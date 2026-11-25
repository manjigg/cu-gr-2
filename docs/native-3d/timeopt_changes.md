# TimeOpt 代码修改记录

## M0 测量

| 文件与函数 | 修改与验证 |
|---|---|
| src/native/Profile.h、Profile.cpp | 作用域计时、独占时间、计数及逐网记录，算法预算保持原值 |
| src/main.cpp、src/native/NativeRouter.cpp、MultiDieDesign.cpp | 区分旧路由计时、输入、网格初始化、初解、阶段、候选生成和输出 |
| src/gr/PatternRoute.cpp、src/native/PatternSearch.cpp、RoutingGraph.cpp、SeedTopology.cpp、BranchRepair.cpp、SparseMaze.cpp | 在实际执行点记录 FLUTE、模式 DP、HBT、资源、迷宫、修复、验证与事务开销 |
| scripts/native_bench.py、native_profile.py | 可复现计时开关、固定线程和主问题预算，聚合时核验独占时间守恒 |

## M1 资源与事务

| 文件与函数 | 修改与正确性保证 |
|---|---|
| src/native/RoutingGraph.h、RoutingGraph.cpp 的 pointId、resourceId、location | 按边类型和 Die 偏移给 Wire/Via 编号。HBT 独立以物理 site ID 编号。编号顺序保留原五元组的字典序。为布局体积加溢出检查 |
| RoutingGraph::capacity、countTracks、landingFootprint | 沿各层的一个网格轴缓存轨道数与 Via 最小面积分数需求。常见网格采用上限 40 MiB 的静态容量数组，更大输入仍使用逐轴缓存。账本更新不改变静态容量。开关 enable_resource_cache 可关闭静态缓存 |
| RoutingGraph::prepareRoute、commitPrepared、replacePrepared、release | 摘要记录每个唯一资源的累计需求、线长、Via、HBT 与固定成本。静态树连通性和固定站点先验证。复用摘要时按当前账本检查容量、站点归属、间距。替换按排序的两个摘要预检差量，并在成功分配所有新键后发布 |
| RoutingGraph::availableSites | 以站点空间索引查询间距窗口，再用原欧氏距离和站点归属规则判定 |
| src/native/GuideOutput.cpp、tests/native/replay.cpp | 用 location 从稳定 ID 读取 Die 与边类型。回放器重建账本并逆序释放 |
| tests/native/resources.cpp | 同一网格对比缓存与无缓存的每条邻接资源和代价。测试替换因他网占用站点而失败时资源原样保留，资源腾出后替换成功，释放后账本为空 |

M1 的空间查询、缓存与差量更新保留原硬约束和候选几何。Release 构建和四组 CTest 均通过，AES 与 ethmac 的同预算测量单独保存。[M1 回归](../../result-3/timeopt/_profiles/ctest-m1.log)

## M2–M6

| 文件与函数 | 修改、搜索空间与验证 |
|---|---|
| src/native/NativeRouter.cpp、IncrementalRoute.cpp | exact 保留完整标签和全网候选池。fast 保留已提交的当前解，只有选中网和冲突组参与撤销和重布。候选被拒绝或超时后复用旧摘要恢复。按网预算和阶段预算单独记录。 |
| src/native/PatternSearch.cpp 的 projectTopology、enumerateFast | 缓存固定终端投影。用排序单位边向量做线性并集，减少树节点分配，局部路径缓存仅在账本不变的一次搜索内有效。Beam 保留综合代价、HBT、线长、Via 和站点多样性。Beam 只用于 fast。 |
| src/native/RoutingGraph.cpp 的 seedLanding、sitesNearRegion、singleHbtLowerBound | 最近站点数逐步翻倍，完全等距且可用的并列站点可沿用基准选择。三维 Pattern 用完整加权 L1 中位数矩形查询邻居。两侧格中心分别计算独立的 L1 和金属层转换下界。fast 路径池的上界余量会改变候选空间。 |
| src/native/ConflictOptimizer.cpp、IncrementalRoute.cpp 的 optimizeConflicts | 按预测的多网累计需求和 HBT 间距边构建冲突分量，账本保持可行。用候选混合预测需求更新独立价格，再用原固定代价接受结果。局部分支定界在当前未超时的候选池上精确，超时保留已知可行上界。 |
| src/native/SparseMaze.cpp 的 MazeWorkspace、grow、seed | 整数节点 ID、较小图采用代次数组，较大图采用稀疏状态哈希。fast 优先尝试局部封装坐标窗口，失败尝试完整窗口及原来的多级稀疏步长。合并新路径计算未共享边的差量，最终整树依然验证。 |
| tests/native/pattern.cpp、test_flow.py | 覆盖局部联合选择优于争用贪心、候选与外部资源同时可行、fast 初解与回滚、原有三 HBT 及跨 Die 绕行。 |

早期快速样本保留 BP FLUTE 整数范围失败。后续将高扇出网交给保持端点身份的分簇初解、合法分支修复和迷宫回退。投影 DP 处理其数值范围覆盖的网络。[原始失败输出](../../result-3/timeopt/_ablation/v5/bp/router.log)
