#include "Profile.h"
#include "RoutingGraph.h"
#include <numeric>

namespace native3d {
namespace {
const char* timings[]={
    "total","input","initialization","seed","local_seed","cross_die_seed","flute","pattern_dp",
    "hbt_search","branch_repair","sparse_maze","available","resources","cost","capacity","step_cost",
    "validate","commit","release","stage_1","stage_2","stage_3","candidate_generation","master_solver","guide_output"
};
const char* counters[]={
    "local_paths_calls","hbt_sites_visited","dp_labels_generated","dp_label_merges","dp_labels_pruned",
    "astar_expanded_nodes","master_search_nodes"
};
const char* methods[]={"seed","pattern","detour","maze"};
static_assert(sizeof(timings)/sizeof(*timings)==static_cast<unsigned>(Timing::Count),"Timing names");
static_assert(sizeof(counters)/sizeof(*counters)==static_cast<unsigned>(Event::Count),"Counter names");
static_assert(sizeof(methods)/sizeof(*methods)==static_cast<unsigned>(NetMethod::Count),"Net methods");

double seconds(Profile::Clock::duration d) {return std::chrono::duration<double>(d).count();}
std::ofstream outputFile(const std::string& path) {
    std::ofstream out;
    out.exceptions(std::ios::badbit|std::ios::failbit);
    out.open(path);
    out.precision(12);
    return out;
}
}

thread_local Profile* Profile::active=nullptr;
Profile::Profile() {
    if(active) throw std::logic_error("Profile already active");
    active=this;
}
Profile::~Profile() {active=nullptr;}
void Profile::count(Event event,uint64_t amount) {
    if(auto* p=current()) p->events[static_cast<int>(event)]+=amount;
}
void Profile::labels(size_t live) {
    if(auto* p=current()) p->peakLabels=std::max(p->peakLabels,live);
}
void Profile::retry() {
    if(auto* p=current()) if(p->currentNet>=0) ++p->nets[p->currentNet].retries;
}
void Profile::prepareNets(size_t count) {nets.resize(count);}

Measured::Measured(Timing t):kind(t),profile(Profile::current()) {
    if(!profile) return;
    previous=profile->frame;
    profile->frame=this;
    start=Profile::Clock::now();
}
Measured::~Measured() {
    if(!profile) return;
    const auto elapsed=Profile::Clock::now()-start;
    auto& t=profile->times[static_cast<int>(kind)];
    ++t.calls;
    t.inclusive+=elapsed;
    t.exclusive+=elapsed-children;
    if(previous) previous->children+=elapsed;
    profile->frame=previous;
}
MeasuredNet::MeasuredNet(int id,NetMethod m):profile(Profile::current()),net(id),method(m) {
    if(!profile) return;
    previous=profile->currentNet;
    profile->currentNet=net;
    parent=profile->netFrame;
    profile->netFrame=this;
    start=Profile::Clock::now();
}
MeasuredNet::~MeasuredNet() {
    if(!profile) return;
    const auto elapsed=Profile::Clock::now()-start;
    profile->nets[net].time[static_cast<int>(method)]+=elapsed-children;
    if(parent) parent->children+=elapsed;
    profile->netFrame=parent;
    profile->currentNet=previous;
}

void Profile::describeNets(const MultiDieDesign& model,const RoutingGraph& graph,const std::vector<Route>& routes) {
    std::vector<int> ids;
    for(const auto& net:model.nets) ids.push_back(net.id);
    auto total=[&](int net) {return std::accumulate(nets[net].time.begin(),nets[net].time.end(),Clock::duration{});};
    auto end=ids.begin()+std::min<size_t>(100,ids.size());
    std::partial_sort(ids.begin(),end,ids.end(),[&](int a,int b) {
        return std::make_pair(-total(a),a)<std::make_pair(-total(b),b);
    });
    for(auto it=ids.begin();it!=end;++it) {
        int id=*it;
        if(total(id)==Clock::duration{}) break;
        const auto& net=model.nets[id];
        NetRecord row{id,{},{{INT64_MAX,INT64_MAX,INT64_MIN,INT64_MIN}},graph.terminals(id).size(),0,0};
        for(int die:{0,1}) if(net.localNet[die]>=0) {
            row.dieMask|=1U<<die;
            row.names[die]=model.dies[die]->design->getAllNets().at(net.localNet[die]).getName();
        }
        for(const auto& pin:graph.terminals(id)) for(auto ap:pin) {
            const auto xy=graph.center(ap);
            for(int axis:{0,1}) {
                row.box[axis]=std::min(row.box[axis],xy[axis]);
                row.box[axis+2]=std::max(row.box[axis+2],xy[axis]);
            }
        }
        if(!routes.empty())
            row.hbt=std::count_if(routes[id].edges.begin(),routes[id].edges.end(),
                [](const Edge& e){return e.type==GREdgeType::HBT;});
        slow.push_back(std::move(row));
    }
}
void Profile::write(const std::string& dir) const {
    Json profile;
    profile["clock"]="steady_clock";
    profile["total_scope"]="before_native_input_through_routing_statistics";
    profile["label_peak_scope"]="largest partial DP bucket";
    for(int i=0;i<static_cast<int>(Timing::Count);++i) {
        const auto& t=times[i];
        profile["modules"][timings[i]]={{"calls",t.calls},{"inclusive_seconds",seconds(t.inclusive)},
                                          {"exclusive_seconds",seconds(t.exclusive)}};
    }
    for(int i=0;i<static_cast<int>(Event::Count);++i)
        profile["counters"][counters[i]]=events[i];
    profile["counters"]["dp_peak_labels_in_bucket"]=peakLabels;
    auto json=outputFile(dir+"/profile.json");
    json<<profile.dump(2)<<'\n';
    json.close();
    auto tsv=outputFile(dir+"/slow_nets.tsv");
    tsv<<"net_id\ttop_net\tbottom_net\treal_pins\tdie_mask\tbond_x_low\tbond_y_low\tbond_x_high\tbond_y_high"
          "\thbt\tsearch_seconds\tretries\talgorithms\tseed_seconds\tpattern_seconds\tdetour_seconds\tmaze_seconds\n";
    for(const auto& row:slow) {
        const auto& n=nets[row.id];
        tsv<<row.id;
        for(const auto& name:row.names) tsv<<'\t'<<name;
        tsv<<'\t'<<row.pins<<'\t'<<row.dieMask;
        for(auto coordinate:row.box) tsv<<'\t'<<coordinate;
        tsv<<'\t'<<row.hbt<<'\t'<<seconds(std::accumulate(n.time.begin(),n.time.end(),Clock::duration{}))<<'\t'<<n.retries<<'\t';
        bool combined=false;
        for(size_t i=0;i<n.time.size();++i) if(n.time[i]!=Clock::duration{}) {
            if(combined) tsv<<',';
            tsv<<methods[i];
            combined=true;
        }
        for(auto d:n.time) tsv<<'\t'<<seconds(d);
        tsv<<'\n';
    }
    tsv.close();
}
}
