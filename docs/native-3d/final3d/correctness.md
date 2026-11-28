# 验证方法与代码位置

| 约束或机制 | 实现入口 | 测试与证据 |
|---|---|---|
| 静态与动态可用性分离 | RoutingGraph::staticallyAvailable、dynamicBlockers | native_final_exchange 固定资源与实际 HBT 拥有者对照，原有 native_resource_view 固定站点用例 |
| 物理边等价和安全支配 | CandidateArchive::insert、resourceDominates | 具有相同站点但额外金属边的完整候选可删除，不同站点候选继续保留 |
| 资源交换 | ConflictOptimizer::selectGroup | 人工两网使用站点互换，单网组合拒绝，联合可提交 |
| 预算交换 | IncumbentSolution::replaceJoint、selectGroup | 人工预算三站点，供给网放弃一个站点且自身成本增加，需求网增加一个站点并降低线长，联合成本下降 |
| 精确枚举与时限 | ConflictOptimizer::solveChoices | 有界搜索与关闭剪枝完整枚举一致，组外已超出预算也保留超额优先，协作式时限上报未完成 |
| 多资源累加、原子替换 | RoutingGraph::groupDemand、canReplaceGroup、replaceGroup | native_conflict_repair 三网累计通孔需求和资源索引检查，native_hbt_footprint 两侧接入需求检查 |
| 候选版本和后续恢复 | replaceJoint、ResourceReadView 版本检查 | 过期视图和过期线网候选拒绝，失败后账本一致，全部旧资源释放后为空 |
| 相同初解 | MazeExperiment 的 Stage 2 快照入口 | native_final_stage2 冻结物理树和 Stage 1 候选，旧路径回放输出相同，同快照执行新路径与独立检查 |
| 既有布线入口 | NativeRouter、原二维 route | native_import、native_flow、native_pattern、native_parallel_maze、native_2d_regression |

ASan（地址检查）、UBSan（未定义行为检查）和 TSan（线程竞争检查）使用独立动态链接构建。初次 UBSan 日志只报告系统 Boost 1.71 的 serialization/singleton.hpp 第 181 行，另跑以该系统头路径为范围的原有抑制文件并启用错误即停止。地址检查关闭遗留库的进程退出泄漏统计。Release 产物用于正式实验。

对正式物理全局结果先用 C++ native_replay 读回每条导线、通孔和 HBT 边，占用后逆序释放全部资源并检查空账本。再用独立 Python native_check 核对来自原始输入的终端归属、站点阵列、变换、两侧金属层和跨裸片连通。预算专项允许核对一个尚有超额的物理解，并保留超额状态。详细实验由 OpenROAD/TritonRoute 生成两侧独立几何，OpenDB 导出导体后执行原双裸片联合导体检查。实验结论覆盖所用资源模型和该验证流程。
