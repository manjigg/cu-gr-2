#include "FailureRepair.h"
#include "SparseMaze.h"

namespace native3d {
Json repairFailedNets(IncumbentSolution& solution,const std::vector<std::pair<double,int>>& priority,
                      const Deadline& stage,double seconds,int stride) {
    auto& g=solution.graph;
    const int groupLimit=integer(g.model.config.value("failure_group_limit",Json(4)));
    if(groupLimit<2 || groupLimit>16) throw std::invalid_argument("Invalid failure group size");
    size_t failed=0,direct=0,groups=0,changed=0,timeout=0,unresolved=0,noOwner=0;
    const auto start=std::chrono::steady_clock::now();
    for(const auto& item:priority) {
        if(stage.expired()) break;
        const int net=item.second;
        auto& entry=solution.current[net];
        if(entry.summary) continue;
        ++failed;
        MazeEffort observed;observed.recordBlockers=true;
        Deadline diagnose(stage,seconds);
        try {
            auto paths=SparseMaze(g,net,diagnose,nullptr,&observed).seed(stride,true);
            if(!paths.empty()) {
                auto prepared=g.prepareRoute(net,paths.front());
                solution.publish(net,std::move(paths.front()),prepared);++direct;continue;
            }
        } catch(const SearchTimeout&) {++timeout;}
        std::map<int,size_t> frequency;
        for(const auto& hot:observed.blocked) for(int owner:g.users(hot.first)) frequency[owner]+=hot.second;
        std::vector<std::pair<int,int>> rank;
        for(const auto& score:frequency) rank.emplace_back(-int(score.second),score.first);
        std::sort(rank.begin(),rank.end());
        if(rank.empty()) {++noOwner;continue;}
        if(g.model.config.value("enable_iterative_resource_pricing",false))
            g.priceBlockedRequests(observed.blocked,g.model.config.value("pricing_step",0.5),
                g.model.config.value("pricing_decay",0.75),g.model.config.value("pricing_ceiling",8.0));
        std::vector<int> ids{net};
        for(const auto& owner:rank) if(ids.size()<size_t(groupLimit)) ids.push_back(owner.second);
        ++groups;
        std::vector<PreparedRoute> summaries;
        std::vector<Route> trial;
        // 所有临时路径仅进入只读视图的稀疏增量，正式账本始终保留旧树。
        Deadline limit(stage,seconds*ids.size());
        try {
            for(int id:ids) {
                auto view=g.groupView(id,ids,summaries);
                const auto& old=solution.current[id];
                if(old.summary && g.available(*old.summary,&view)) {
                    summaries.push_back(old.summary);trial.push_back(old.route);continue;
                }
                auto alternatives=SparseMaze(g,id,limit,&view).seed(stride,true);
                if(alternatives.empty()) break;
                auto next=g.prepareRoute(id,alternatives.front());
                if(!g.available(*next,&view)) break;
                summaries.push_back(next);trial.push_back(std::move(alternatives.front()));
            }
        } catch(const SearchTimeout&) {++timeout;}
        if(trial.size()!=ids.size() || !g.replaceGroup(ids,summaries)) {++unresolved;continue;}
        for(size_t i=0;i<ids.size();++i) {
            auto& old=solution.current[ids[i]];
            solution.hbtTotal=solution.after(old.summary,summaries[i]);
            old.route.edges.swap(trial[i].edges);old.route.root=trial[i].root;
            old.summary=std::move(summaries[i]);++old.version;old.failures=0;
        }
        ++changed;
    }
    return {{"unrouted_attempted",failed},{"direct_repairs",direct},{"groups_attempted",groups},
            {"groups_published",changed},{"search_timeouts",timeout},{"unresolved_groups",unresolved},
            {"no_observed_owner",noOwner},{"seconds",std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()}};
}
}
