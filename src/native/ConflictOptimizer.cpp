#include "ConflictOptimizer.h"
#include "Profile.h"
#include <numeric>

namespace native3d {
ConflictGroups buildConflictGroups(const RoutingGraph& graph,const std::vector<std::vector<PreparedRoute>>& byNet,
                                   size_t maxGroup,double step) {
    struct Entry {
        std::vector<int> users;
        double active=0,potential=0,projectedDelta=0;
    };
    std::map<Resource,Entry> demand;
    for(size_t id=0;id<byNet.size();++id) if(byNet[id].size()>1) {
        for(size_t i=1;i<byNet[id].size();++i) for(const auto& r:byNet[id][i]->resources())
            demand[r.first];
    }
    for(size_t id=0;id<byNet.size();++id) {
        const auto& candidates=byNet[id];
        if(candidates.empty()) continue;
        Demand maximum;
        for(const auto& candidate:candidates) for(const auto& r:candidate->resources())
            if(demand.count(r.first)) maximum[r.first]=std::max(maximum[r.first],r.second);
        for(const auto& r:maximum) {
            auto& entry=demand.at(r.first);
            entry.users.push_back(id);
            entry.potential+=r.second;
        }
        for(const auto& r:candidates.front()->resources()) {
            auto it=demand.find(r.first);
            if(it!=demand.end()) {it->second.active+=r.second;it->second.projectedDelta-=r.second;}
        }
        // 候选均匀混合作为独立的预测需求。价格只引导候选搜索，预测不会写入账本。
        for(const auto& candidate:candidates) for(const auto& r:candidate->resources()) {
            auto it=demand.find(r.first);
            if(it!=demand.end()) it->second.projectedDelta+=r.second/candidates.size();
        }
    }
    std::vector<int> parent(byNet.size());
    std::iota(parent.begin(),parent.end(),0);
    auto root=[&](int id) {
        while(parent[id]!=id) {parent[id]=parent[parent[id]];id=parent[id];}
        return id;
    };
    auto group=[&](const std::vector<int>& ids) {
        for(int id:ids) parent[root(id)]=root(ids.front());
    };
    ConflictGroups result;
    for(const auto& r:demand) {
        const auto loc=graph.location(r.first);
        const auto actual=graph.demand().find(r.first);
        const double used=actual==graph.demand().end()?0.:actual->second;
        const double cap=loc.type==GREdgeType::HBT?1.:graph.capacity(r.first);
        const auto& users=r.second.users;
        if(users.size()>1 && r.second.potential+used-r.second.active>cap+1e-12) {
            group(users);
            ++result.contested;
        }
        const double price=step*std::max(0.,used+r.second.projectedDelta-cap);
        if(price>0) result.prices.emplace(r.first,price);
        if(loc.type!=GREdgeType::HBT || users.empty()) continue;
        for(int id:graph.spacingNeighbors(loc.x)) {
            auto conflict=demand.find(graph.hbtResource(id));
            if(conflict==demand.end() || conflict->second.users.empty()) continue;
            parent[root(users.front())]=root(conflict->second.users.front());
            group(users);group(conflict->second.users);
        }
    }
    std::map<int,std::vector<int>> components;
    for(size_t id=0;id<byNet.size();++id) if(!byNet[id].empty()) components[root(id)].push_back(id);
    for(auto& item:components) {
        auto& ids=item.second;
        if(ids.size()<2 || std::none_of(ids.begin(),ids.end(),[&](int id){return byNet[id].size()>1;})) continue;
        for(size_t start=0;start+1<ids.size();start+=maxGroup) {
            auto first=ids.begin()+start,last=ids.begin()+std::min(ids.size(),start+maxGroup);
            if(last-first>=2) result.groups.emplace_back(first,last);
        }
    }
    return result;
}

namespace {
struct SelectionContext {
    bool protect=false,exhaustive=false;
    size_t budget=std::numeric_limits<size_t>::max(),outsideHbt=0;
    const ResourceReadView* view=nullptr;
};
template<class Cost>
LocalResult solveChoices(RoutingGraph& graph,const std::vector<int>& ids,
                         const std::vector<const std::vector<PreparedRoute>*>& choices,
                         const Deadline& deadline,const SelectionContext& ctx) {
    Measured measured(Timing::Master);
    LocalResult result;
    result.selection.assign(ids.size(),0);
    std::vector<size_t> chosen(ids.size());
    std::vector<PreparedRoute> combination(ids.size());
    std::vector<double> suffix(ids.size()+1,0.);
    std::vector<Cost> minima(ids.size());
    std::vector<size_t> suffixHbt(ids.size()+1);
    Cost best=0;
    size_t hbtBudget=0;
    for(size_t i=ids.size();i>0;) {
        --i;
        const auto& candidates=*choices[i];
        if(candidates.empty()) throw std::invalid_argument("Empty local candidate set");
        combination[i]=candidates.front();
        if(!ctx.view) best+=candidates.front()->cost();
        hbtBudget+=candidates.front()->hbtSites().size();
        const auto cheap=std::min_element(candidates.begin(),candidates.end(),
            [](const PreparedRoute& a,const PreparedRoute& b){return a->cost()<b->cost();});
        suffix[i]=std::nextafter(suffix[i+1]+(*cheap)->cost(),0.);
        minima[i]=(*cheap)->cost();
        if(ctx.view) {
            const auto minHbt=std::min_element(candidates.begin(),candidates.end(),
                [](const PreparedRoute& a,const PreparedRoute& b){return a->hbtSites().size()<b->hbtSites().size();});
            suffixHbt[i]=suffixHbt[i+1]+(*minHbt)->hbtSites().size();
        }
    }
    if(ctx.view) {
        if(!graph.canReplaceGroup(ids,combination,*ctx.view)) throw std::logic_error("Invalid incumbent combination");
        for(const auto& incumbent:combination) best+=incumbent->cost();
    }
    auto excess=[&](size_t h){
        const size_t total=ctx.outsideHbt+h;
        return total>ctx.budget?total-ctx.budget:0;
    };
    auto bestExcess=excess(hbtBudget);
    const Cost tolerance=Cost(1e-9L);
    std::function<void(size_t,Cost,size_t)> search=[&](size_t i,Cost value,size_t hbt) {
        deadline.check();
        if(i==ids.size()) {
            ++result.combinations;
            if(ctx.view && !graph.canReplaceGroup(ids,combination,*ctx.view)) return;
            if(excess(hbt)<bestExcess || (excess(hbt)==bestExcess && value+tolerance<best)) {
                bestExcess=excess(hbt);best=value;result.selection=chosen;
            }
            return;
        }
        const auto id=ids[i];
        for(size_t k=0;k<choices[i]->size();++k) {
            if(ctx.view) deadline.check();
            ++result.visited;
            Profile::count(Event::MasterNodes);
            const auto& candidate=(*choices[i])[k];
            const Cost next=value+candidate->cost();
            const auto nextHbt=hbt+candidate->hbtSites().size();
            if(ctx.view) {
                if(!ctx.exhaustive) {
                    const auto lowerExcess=excess(nextHbt+suffixHbt[i+1]);
                    // 同一求和值顺序中逐项取最小值。浮点加法对各项单调，避免异序后缀高估。
                    Cost lower=next;
                    for(size_t j=i+1;j<minima.size();++j) lower+=minima[j];
                    if(lowerExcess>bestExcess || (lowerExcess==bestExcess && lower+tolerance>=best)) {
                        ++result.pruned;
                        continue;
                    }
                }
                combination[i]=candidate;
                chosen[i]=k;
                search(i+1,next,nextHbt);
                continue;
            }
            if(ctx.protect && nextHbt>hbtBudget) continue;
            if(excess(nextHbt)>bestExcess || (excess(nextHbt)==bestExcess && next+suffix[i+1]>best+1e-9)
               || !graph.available(*candidate)) continue;
            graph.commitPrepared(id,candidate);
            chosen[i]=k;
            try {search(i+1,next,nextHbt);}
            catch(...) {graph.release(id);throw;}
            graph.release(id);
        }
    };
    try {search(0,0.,0);}
    catch(const SearchTimeout&) {result.complete=false;}
    result.bestCost=best;
    result.bestExcess=bestExcess;
    return result;
}
}
LocalResult selectLocal(RoutingGraph& graph,const std::vector<int>& ids,
                        const std::vector<std::vector<PreparedRoute>>& choices,const Deadline& deadline,
                        bool protectHbt,size_t hbtRoom) {
    std::vector<const std::vector<PreparedRoute>*> selected;
    for(int id:ids) selected.push_back(&choices.at(id));
    SelectionContext context;
    context.protect=protectHbt;
    context.budget=hbtRoom;
    return solveChoices<double>(graph,ids,selected,deadline,context);
}
LocalResult selectGroup(RoutingGraph& graph,const std::vector<int>& ids,
                        const std::vector<std::vector<PreparedRoute>>& choices,const Deadline& deadline,
                        size_t hbtOutside,size_t hbtBudget,bool exhaustive) {
    if(ids.empty() || ids.size()!=choices.size()) throw std::invalid_argument("Invalid local choices");
    const auto view=graph.groupView(ids.front(),ids);
    std::vector<const std::vector<PreparedRoute>*> selected;
    for(const auto& pool:choices) selected.push_back(&pool);
    SelectionContext context;
    context.view=&view;
    context.outsideHbt=hbtOutside;
    context.budget=hbtBudget;
    context.exhaustive=exhaustive;
    return solveChoices<long double>(graph,ids,selected,deadline,context);
}
}
