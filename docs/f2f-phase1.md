# F2F 第四节：现有 HBT 候选搜索改进

对照是 `one-3d-change-from-2d-baseline` 的 `991dd4b`，诊断提交为 `9f75a22`。第三节源码核对、问题证据与原算法记录见 [诊断](f2f-phase1-diagnosis.md)。本轮改变外层 HBT 候选与重布接受策略。Wire、Via 和 HBT 的统一异构图、跨 Die Maze、资源感知构树及跨 Die Pattern DAG 属于后续阶段。

## 算法和数据流

```text
真实 Pin + 封装坐标 → 现有二维 FLUTE 投影树
  → 单 HBT 基础拓扑 + DP(v,die,k) 的多数量拓扑
  → 每个跨越对应的 L1 区域 + 可用物理 SiteId
  → Beam 组合、按真实候选代价扩大搜索
  → 分量级 Pattern / Detour / 真实金属层局部 Maze
  → 合法性、接入点连通、实际 Wire/Via/HBT 和溢出
  → 首次提交，或者与同资源快照下的旧方案比较
```

### 数量 DP

新增 `F2fLabelNode`（节点本地代价和子边 Die 转移矩阵）及 `F2fDieAssignment`（回溯 Die、HBT 数和估计代价）。`searchF2fDieAssignments()` 在每个 `(v,die,k)` 保存两条最佳回溯链。初始化计入当前顶点连接对侧终端所需的 0/1 颗 HBT。合并子分支时用树形背包卷积，左侧已用预算加子树预算及父子 Die 不同产生的 1。根按每个 HBT 数量最多回溯两组标签，生成的跨越数必须与 DP 的 k 相同。

排序代价含按 Die 物理距离换算的 Wire 成本、方向层上抽样的现有拥塞成本、普通 Via 接入和每个跨越的 HBT 费用。该估计只负责生成候选，最终路由仍用真实边计价。默认上限为 8 颗 HBT 和 160 个投影节点；超节点上限时用旧两组标签的有界候选，并继续评估单 HBT 基础方案。预算 DP 在固定树和可加估价下合并状态，复杂度约为 O(N × K² × B²)，B 为每状态保留数，默认 2。FLUTE 的 C 内核、查找表和单 Die Pattern/Maze 主体未改。

### 自适应组合搜索

每个跨越从已有 L1 区域及 R-tree 查询中取合法物理站点。按候选数较少的锚点优先展开，Beam（束搜索）默认宽度 3，组合中按物理 SiteId 排除同一站点。多落点共享关系部分由每个 Die 内分量的包围盒增量近似，另加入接入 Via、局部拥塞采样和离 L1 区域的距离。包围盒是空间覆盖估计，最终线长由实际局部树的单位导线边得到。

首阶段每锚点请求 6 个站点，重布请求 10 个。当没有可行方案，或按真实路由成本得到的本拓扑最佳方案仍用了候选后半区的站点，搜索宽度翻倍到默认上限 32。站点组去重后才做真实布线评价。固定站点保留原所有权，同一 G-cell 内的多个物理 SiteId 仍可分别使用。

### 合法性、记账及接受规则

- `validTopology()` 检查 SiteId、占用、固定站点、每个真实终端只出现一次，以及跨 HBT 分量图是连通树。准备接纳的候选还逐分量检查真实接入点被实际 Wire/Via 覆盖、段方向和换层合法性。
- F2F 改进模式对单分量及多分量统一按物理 Wire/Via 单位步去重，计价、提交和撤销采用同一口径。旧模式保留原单分量行为。Via 最小面积需求仍由原 `visitViaWires()` 计算。
- Stage 2/3 释放旧路由与旧的可移动站点，再对旧方案与新候选按同一外部占用重新估价。按 `导线溢出增量 → 含 Via 足迹的资源溢出增量 → 实际综合成本 → HBT 数` 依次比较。无可行候选或没有严格改善时恢复原树和原站点集合。固定站点在整个过程中继续保留。受控测试比较每阶段已接受方案的逐网长度/Via 和全图累计值，检查回退后的资源记账。

## 代码差异索引

| 文件 | 本轮核心改动 |
|---|---|
| [F2fCandidateSearch.h](../src/gr/F2fCandidateSearch.h)、[F2fCandidateSearch.cpp](../src/gr/F2fCandidateSearch.cpp) | 新增数量预算 DP、两条迹线和固定投影树回溯。 |
| [F2fRouter.cpp](../src/gr/F2fRouter.cpp) | `CandidatePolicy`、`budgetTopologies()`、`beamSites()`、`planJob()`、`validTopology()`、`routeJobs()`。旧模式用于可重复对照。 |
| [GridGraph.cpp](../src/gr/GridGraph.cpp)、[GridGraph.h](../src/gr/GridGraph.h) | `collectForestSteps()`、`getForestCost()`、`measureForest()`、`commitForest()` 和 `connectsPins()`。抽取和验证实际边、成本分项及增量溢出。 |
| [GlobalRouter.h](../src/gr/GlobalRouter.h) | 增加 const 图访问，供候选估价读取。 |
| [F2fCandidateSearchTest.cpp](../tests/h3d/F2fCandidateSearchTest.cpp)、[test_candidate_phase1.py](../tests/h3d/test_candidate_phase1.py) | 小树枚举对齐每个 k 的导出数量和前两名代价。集成验证两 Pin、站点冲突与耗尽、镜像、异网格、同 G-cell 不同物理站点及有溢出时的新旧方案和资源恢复。原数量测试覆盖双簇/三簇并验证落点 Guide。 |

配置默认 `"candidate_mode": "improved"`。`"legacy"` 选择旧拓扑、位置和重布规则。`candidate_search` 支持 `budget_dp`、`position_beam`、`compare_old`、`deduplicate_steps` 开关和 `max_hbt`、`max_projection_nodes`、`beam_width`、`max_sites` 上限；开启 `diagnostics` 路径后输出逐网 JSONL。以下在项目根目录运行：

```bash
cmake -S src -B /tmp/cugr-f2f-build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/cugr-f2f-build -j 6
c++ -std=c++14 -O2 tests/h3d/F2fCandidateSearchTest.cpp src/gr/F2fCandidateSearch.cpp -o /tmp/f2f-dp-test
/tmp/f2f-dp-test
python3 tests/h3d/test_hbt_count.py /tmp/cugr-f2f-build/route
python3 tests/h3d/test_candidate_phase1.py /tmp/cugr-f2f-build/route
python3 scripts/f2f_phase1_bench.py /tmp/cugr-f2f-build/route results-3/f2f-phase1/replay-legacy --mode legacy
python3 scripts/f2f_phase1_bench.py /tmp/cugr-f2f-build/route results-3/f2f-phase1/replay-improved
```

## 参照实验口径

默认七组对照使用相同的 `benchmarks/` LEF/DEF、切分端口映射、原点、3 µm 阵列、HBT 单价 4、`metal7` 和恒等坐标变换；单线程、Release 构建、同样启用诊断。计时含诊断开销，且实验机同时执行了参考构建及验证，时间用于描述本轮实际开销。

`h3d-dp` 的论文模式另从 `f806117` 构建，临时适配器仅在内部按相同的切分端口映射排除旧占位并在派生 DEF 中移除这些旧占位。原输入不变。它自带按 800 DBU pad、金属间距和 landing 障碍物筛选站点的实现，合法站点集合与默认 F2F 不同。原始日志全部保留。mor1kx 的外围 3 µm 中心站点距边界 310 DBU，小于 pad 半宽 400 DBU，参考模式拒绝输入。单独给 AES 的两种 F2F 模式都禁用参考版筛掉的 100 个站点，比较 2,501 个同一位置的站点。参考版随后还将已分配 pad 计入局部容量，指标作为参考并保留这一差异；默认七组之间的改善比例只由 F2F 同条件两列计算。

## 七组全局结果

线长单位为 µm，HBT 和 Via 为个数。两组所有退出码均为 0，未连通网、HBT 站点冲突、最终总 Wire Overflow 和最大单边 Wire Overflow 均为 0。文件检查确认七组的跨 Die 网数量与输入一致，两份 Guide 都覆盖对应 HBT 落点。

| 基准 | 旧线长 | 新线长 | 线长变化 | 旧 HBT | 新 HBT | 旧 Via | 新 Via |
|---|---:|---:|---:|---:|---:|---:|---:|
| aes | 137447.100 | 136308.900 | −0.828% | 388 | 381 | 90718 | 90320 |
| tinyRocket | 321105.190 | 312740.610 | −2.605% | 2004 | 1976 | 156746 | 155571 |
| ethmac | 384190.400 | 380419.375 | −0.982% | 3969 | 3785 | 167671 | 166158 |
| jpegencode | 2403733.185 | 2381074.185 | −0.943% | 17736 | 17178 | 1093992 | 1087853 |
| mor1kx | 1290320.790 | 1304728.890 | +1.117% | 6178 | 5114 | 416063 | 407965 |
| ariane | 3037125.350 | 3024399.000 | −0.419% | 15973 | 15558 | 1043044 | 1036462 |
| bp | 4595283.280 | 4592074.480 | −0.070% | 4217 | 4150 | 1290591 | 1292087 |
| 合计 | 12169205.295 | 12131745.440 | −0.308% | 50465 | 48142 | 4258825 | 4236416 |

六组同时缩短线长并减少 HBT。HBT 合计减少 4.60%，Via 合计减少 0.53%。两版均开诊断，内部时间合计从 181.37 s 到 784.56 s，墙钟从 211.35 s 到 815.29 s。峰值 RSS 和逐例时长详见 [comparison.tsv](../results-3/f2f-phase1/comparison.tsv)。三个阶段的 Top/Bottom 线长、溢出、Via、HBT、接受和恢复次数见 [stages.tsv](../results-3/f2f-phase1/stages.tsv)。

### 逐网归因、消融与扫描

- mor1kx 的旧版有 169 条网使用超过 8 颗 HBT。默认改进版在这组网减少 1,635 颗并增加 27,955.2 µm；原 HBT 数在 1–8 的 2,758 条网合计缩短 13,538.7 µm。这说明新增线长主要集中在被预算压低数量的网。仅把此例上限从 8 改为 16，线长变为 1,280,211.39 µm，HBT 增到 7,007，纯导线溢出仍为 0。相比旧版缩短约 0.78%，但用了更多 HBT。该单变量对照与七组统一默认设置独立记录。
- 在 AES 保持 HBT 单价 4 时，关闭预算 DP 得 137,235.0 µm / 387 HBT；关闭位置 Beam 得 136,296.3 µm / 381 HBT；关闭单分量去重得 136,323.6 µm / 381 HBT；全开为 136,308.9 µm / 381 HBT。这里 DP 带来主要改善，Beam 在 AES 的这一组参数下有 12.6 µm 的退化，去重小幅影响指标。消融结果显示重复计数只解释了小部分线长差。
- 回退探针用有限站点数构造拥塞，记录 3 次不改善的旧方案恢复。启用 `compare_old` 时实际接受 67 次重布，关闭后接受 71 次。回归逐阶段重算 Via 和线长，和已提交资源计数一致。另保存了无合法 HBT 的失败用例，程序明确返回失败。
- AES 的 HBT 单价 0/1/4/8/16 都落在 381 颗、136,308.9 µm。100 网人工拥塞用例的观察前沿有多个互不支配的点。原实现（单价 1）为 192 颗 / 16,751.7 µm；改进实现单价 1 为 241 颗 / 15,439.2 µm，单价 64 为 219 颗 / 15,863.4 µm。顺序提交和有限候选使扫描不保证单调。 [线长与 HBT 数量的观察 Pareto 图](../results-3/f2f-phase1/pareto.png)
- 用 `2d-baseline` 单独编译的原版和当前版分别跑 ISPD18 样例，单 Die Guide 逐字节相同。新版 `legacy` 模式的 AES Guide/TSV 与诊断基线逐字节相同。原七组诊断版和上轮 `change3dResultsOne` 的对应 Guide/TSV 也逐字节相同。

AES 对齐参考合法站点的控制中，F2F legacy 为 137,747.4 µm / 387 HBT，F2F improved 为 136,720.5 µm / 381 HBT，h3d-dp paper 为 137,008.2 µm / 501 HBT。三者候选物理站点相同（2,501），参考版仍按自己的 pad 容量规则布线。七例未对齐站点的参考数值单独存放。 [参考结果与失败状态](../results-3/f2f-phase1/h3d-reference/summary.tsv)

结果目录保存逐例输入配置、原 Benchmark SHA-256、命令、诊断 JSONL（gzip）、Guide、HBT TSV、实际运行日志。 [实验目录](../results-3/f2f-phase1/)

## 详细布线核验范围

本轮详细布线复核覆盖 AES。用临时导出适配器取实际跨 Die 方案的局部分量，将新 HBT 落点写入派生 DEF，按分量生成 TritonRoute Guide。适配器重放必须与本轮 AES 的两份原始 Guide 和 HBT TSV 逐字节一致才进入 TritonRoute。原 LEF/DEF 未改。最终 DRC、详细线长、普通 Via 数及两侧/跨 Die 连通由 [详细布线表](../results-3/f2f-phase1/detailed-aes/summary.tsv) 和 [逐侧与联合几何检查](../results-3/f2f-phase1/detailed-aes/) 提供，统计口径与上一节全局布线分开。全量七例完成的是全局布线，本轮没有将临时派生 DEF 写出器并入正式路由路径。

本阶段在 Stage 2/3 仍由外层分配 HBT 并做局部 Pattern/Maze。可重现的搜索不保证找到三维全局最优，也没有新增纯单 Die 网络借用对侧走绕行的模式。
