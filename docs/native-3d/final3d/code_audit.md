# 版本三源码审计

开发分支 `end-3d-new-f2f` 与冻结参照 `3d-new-f2f` 同起点，提交为 `01723729f51e907797194a1f968b42f472eacf90`。基线源码快照、二进制、构建与测试记录保存在 `result-3/final3D/baseline`。

## 布线主体

`NativeRouter.cpp` 的原生入口在读取双 Die 后建立 RoutingGraph（统一异构资源图），一次读取 FLUTE 的两份原表。初解调用 `initialCandidates`，单 Die 构树仍走 `PatternSearch::localSeed` 的二维斯坦纳树、布线路径 DAG（有向无环图）及金属层动态规划。跨 Die 初解包括加权 L1 中位区域的单站点方案，以及基于平面与层间估价的 Prim 子树分解。Prim 的估价无法代表完整物理三维斯坦纳树的精确代价。

`projectTopology` 的二维投影为跨 Die 动态规划提供骨架，原始终端按终端组映射到骨架。DP 的键保留 `GRPoint(die, layer, x, y)`，候选保存 Wire、Via 与 HBT 实际单位边集合。相同投影位置不合并两个 Die 的真实终端。Stage 3 的 `routeMazeBatches` 已实现只读搜索、屏障、按版本串行提交与冲突重试。既有 Guide 和详细布线接口按切开 HBT 后的同 Die 分量生成派生输出。

## Stage 2 已确认的局限

- `optimizeConflicts` 从初解的 incumbent（当前已提交布线树）与 `alternatives` 构造候选，先于 `refineTopology` 及后续模式布线。共享资源需求与 HBT 间距可建组，生成阶段未得到完整候选的阻塞关系不会进入这一轮组选择。
- 单网循环执行 `candidates.swap(old.alternatives)`，循环结束时只有当前解或一次更新胜出者留下。合法但总代价稍高、可为其他线网释放资源的候选会被销毁。
- `prepareRoute` 确认物理边、连通树与固定站点，计算需求及固定代价。候选自身容量、内部间距及永久固定邻站仍需要独立检查，不能用当时的动态 `available` 代替。
- `TopologyRefinement::reconnect` 原来直接读当前账本。局部重连检查累计需求，第一条扩展失败便结束该候选；其最短路搜索没有包含累计资源状态。Merge 和 Relocation 按顺序消耗共享尝试预算。Augmentation 原可被 Stage 2 选网与拓扑触发条件排除。
- SparseMaze 的搜索和 `refineMaze` 支持视图；`enumerateFast` 和 `evaluateIncrement` 并未贯通视图。原有组再生成在主线程暂时释放组资源，计算后恢复或提交。新增再生成可以复用已有 SparseMaze 视图接口。
- 反向所有者索引原本只在失败修复开关下启用。已有 `blockingResources` 用于该路径，新的阻塞记录需要区分真实动态竞争、自身超容量、固定站点和间距。

## 代价与预算

`baseCost` 将物理线长乘原线长权重，加普通 Via 与 HBT 边费用，再加逐资源的固定价格。固定价格与静态容量及该候选需求有关，启用既有短路面积代理时还使用导线宽度、长度和容量函数。`stepCost` 可以叠加仅用于搜索的动态引导价。完整候选的 `PreparedRoute::cost()` 不读取动态占用，因而组内固定目标可加。HBT 资源按站点 ID 独占，一棵候选树内同一物理站点的重复转接由 `collectHbtSites` 拒绝。

原 Stage 2 总计时包含先执行的 `master_seconds` 冲突调度，再执行独立 `stage_seconds` 模式与拓扑循环，以及阶段准备和结束检查。旧组内再生成、子求解采用各自 Deadline，可能在当前组结束后略超过主截止。新路径需让各子搜索继承一个阶段父截止，并记录结束时的实际超限。旧的 `room=max(0,B-H_outside)` 在组外已超预算时省略了对所有该组组合一致的常量，组内排序仍等价；报告全局预算状态时必须加回差值。

旧 `selectLocal` 信任候选零号为已提交前解，在组被暂时释放时逐项占用、回溯释放，超时返回已找到的改进。旧 `replaceGroup` 预检累计 Wire/Via 需求、HBT 站点独占与间距，并预分配索引及账本条目。新路径应复用同一组需求验证，以避免求解器和最终事务使用不同的浮点累加与容量判断。
