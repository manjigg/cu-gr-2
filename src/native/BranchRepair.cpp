#include "BranchRepair.h"
#include "Profile.h"
#include <queue>
namespace native3d {
namespace {
class Forest {
    std::map<GRPoint,GRPoint> parent;
public:
    GRPoint root(GRPoint p) {
        parent.emplace(p,p);
        auto q=p;
        while(parent.at(q)!=q) q=parent.at(q);
        while(p!=q) {auto next=parent.at(p);parent.at(p)=q;p=next;}
        return q;
    }
    bool connected(GRPoint a,GRPoint b) {return root(a)==root(b);}
    void link(GRPoint a,GRPoint b) {auto u=root(a),v=root(b);if(u!=v) parent.at(u)=v;}
};
}
Routes repairLocalBranches(const RoutingGraph& graph,int net,const Route& candidate,
                          const std::set<GRPoint>& required,const Deadline& deadline) {
    Measured measured(Timing::BranchRepair);
    Forest forest;
    Edges selected;
    Demand inside;
    std::vector<Edge> gaps;
    auto room=[&](const Resource& id,double extra) {
        auto it=graph.demand().find(id);
        auto own=inside.find(id);
        return (it==graph.demand().end()?0:it->second)+(own==inside.end()?0:own->second)+extra
                <=graph.capacity(id)+1e-12;
    };
    auto canTake=[&](const Edge& e) {
        if(selected.count(e)) return true;
        if(!graph.available(net,{e})) return false;
        for(const auto& r:graph.resources({e})) if(!room(r.first,r.second)) return false;
        return true;
    };
    auto take=[&](const Edge& e) {
        if(forest.connected(e.a,e.b)) return;
        selected.insert(e);forest.link(e.a,e.b);
        for(const auto& r:graph.resources({e})) inside[r.first]+=r.second;
    };
    for(const auto& e:candidate.edges) {
        if(forest.connected(e.a,e.b)) continue;
        if(canTake(e)) take(e);else gaps.push_back(e);
    }
    const auto& grid=graph.model.dies[candidate.root.dieIdx]->grid;
    auto attachment=[&](const Edge& gap,int halo) {
        auto src=forest.root(gap.a),dest=forest.root(gap.b);
        auto near=[&](GRPoint p) {
            return p.x>=std::min(gap.a.x,gap.b.x)-halo && p.x<=std::max(gap.a.x,gap.b.x)+halo &&
                   p.y>=std::min(gap.a.y,gap.b.y)-halo && p.y<=std::max(gap.a.y,gap.b.y)+halo;
        };
        using Item=std::pair<double,GRPoint>;
        std::priority_queue<Item,std::vector<Item>,std::greater<Item>> queue;
        std::map<GRPoint,double> distance;
        std::map<GRPoint,Edge> back;
        // 在局部窗口里从整段已保留导体出发，避免为一个坏边重新连接整根大网。
        auto add=[&](GRPoint p) {
            if(near(p) && forest.root(p)==src && distance.emplace(p,0.).second) queue.emplace(0.,p);
        };
        add(gap.a);
        for(int x=std::max(0,std::min(gap.a.x,gap.b.x)-halo);
            x<=std::min(int(grid.getSize(0))-1,std::max(gap.a.x,gap.b.x)+halo);++x)
            for(int y=std::max(0,std::min(gap.a.y,gap.b.y)-halo);
                y<=std::min(int(grid.getSize(1))-1,std::max(gap.a.y,gap.b.y)+halo);++y)
                for(int l=0;l<int(grid.getNumLayers());++l) add({gap.a.dieIdx,l,x,y});
        while(!queue.empty()) {
            deadline.check();
            auto item=queue.top();queue.pop();
            auto p=item.second;
            if(item.first!=distance.at(p)) continue;
            if(forest.root(p)==dest) {
                Edges path;
                while(back.count(p)) {
                    const auto& e=back.at(p);if(!selected.count(e)) path.insert(e);
                    p=p==e.a?e.b:e.a;
                }
                const auto delta=graph.resources(path);
                if(std::any_of(delta.begin(),delta.end(),[&](const Demand::value_type& r){return !room(r.first,r.second);}))
                    continue;
                for(const auto& e:path) take(e);
                return true;
            }
            for(const auto& e:graph.neighbors(p,net)) {
                if(e.type==GREdgeType::HBT) continue;
                auto q=p==e.a?e.b:e.a;
                if(!near(q)||!canTake(e)) continue;
                double next=item.first+(selected.count(e)?0:graph.stepCost(net,e));
                auto old=distance.find(q);
                if(old!=distance.end() && old->second<=next) continue;
                distance[q]=next;back.erase(q);back.emplace(q,e);queue.emplace(next,q);
            }
        }
        return false;
    };
    // 先处理小窗口内能修好的连接，随后扩窗。未接齐的初解交回完整跨 Die 搜索。
    for(int halo:{1,2,4,8}) {
        for(const auto& e:gaps) if(!forest.connected(e.a,e.b)) attachment(e,halo);
        if(std::all_of(required.begin(),required.end(),[&](GRPoint p){return forest.connected(candidate.root,p);})) break;
    }
    if(!std::all_of(required.begin(),required.end(),[&](GRPoint p){return forest.connected(candidate.root,p);})) return {};
    std::map<GRPoint,Edges> adj;
    for(auto it=selected.begin();it!=selected.end();) {
        if(!forest.connected(candidate.root,it->a)) {it=selected.erase(it);continue;}
        adj[it->a].insert(*it);adj[it->b].insert(*it);++it;
    }
    std::vector<GRPoint> leaves;
    for(const auto& kv:adj) if(kv.second.size()==1) leaves.push_back(kv.first);
    while(!leaves.empty()) {
        auto p=leaves.back();leaves.pop_back();
        if(required.count(p) || adj[p].size()!=1) continue;
        auto e=*adj[p].begin();auto q=p==e.a?e.b:e.a;
        selected.erase(e);adj[p].clear();adj[q].erase(e);
        if(adj[q].size()==1) leaves.push_back(q);
    }
    if(!graph.available(net,selected)) throw std::logic_error("Local branch repair ledger mismatch");
    return {{candidate.root,std::move(selected)}};
}
}
