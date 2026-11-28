# 最终原生三维布线开发记录

本轮在 end-3d-new-f2f 实现三维候选与联合求解协同、静态合法候选档案、安全资源支配、真实阻塞所有者索引接入、组视图再生成、有限候选组合优化及联合事务。现有 FLUTE 平面骨架、初解和并行迷宫为这些机制提供基础。真实案例与总体表现分别保留证据。

常规配置采用版本三可靠调度，使用本轮代码独立重跑。新的联合算法可由 tests/native/final3d-joint-options.json 启用。预算配置增加全设计 HBT 约束，以实际超额量和固定目标作字典序接受。预算、配对结果及实际几何状态由各自的实测汇总报告记录。

- [版本三源码和固定目标审计](code_audit.md)
- [算法、数据结构、数学条件与复杂度](algorithm_design.md)
- [独立验证、试验范围和测试入口](correctness.md)
- [冻结及复现命令](experiment_protocol.md)
- [代表性实验、实际交换和选型](experiment_results.md)
- [正式全局结果和开发报告](../../../result-3/final3D/development_report.md)
- [详细布线验证](../../../result-3/final3Ddetail/verification_report.md)
