# Native CUGR-3D 性能剖析与优化

冻结起点为 `f3498c305d8656d39f4e182d29c17fda14568338`，与 `native-3d-f2f` 的分支头一致。开发分支 `timeopt_native-3d-f2f` 起始工作区干净。原实验目录保留。所有基线重新运行使用仓库内 `tests/native/benchmark-array.json`，逐文件验证 LEF/DEF 的 SHA-256。

## M0 性能基线

同一服务器使用 GCC 9.4、CMake Release、单线程、初解 1200 s、各候选阶段和主问题分别 15 s。先跑 aes、ethmac、ariane、bp，随后运行另外三个。使用冻结的原版二进制重新运行得到 V0，SHA-256 为 `dddc3d7cce3c2a4ede25ce0426f1516c039be7be89e95bc11441df835d988e7e`。计时版执行同一算法并写出嵌套计时，用于定位热点。GNU time 的进程耗时用于正式比较，避免将计时版的探针开销计为原版算法开销。

原 `routing_seconds` 从 `GlobalRouter::route` 创建并检查输出目录后开始，到初解、三阶段候选与 Guide 输出完成后结束，包含 RoutingGraph 构建、FLUTE LUT 初始化及解统计。LEF/DEF 导入、MultiDieDesign 内部的两侧 GridGraph 构建、统计 JSON 最终写盘及对象析构属于进程时间。新增 `Profile::Total` 从原生输入前覆盖到 `GlobalRouter::route` 返回和设计销毁。两个口径均保留。

每个 `Measured` 用 steady_clock 测量整段时间，退出时从该层耗时减去直接子作用域的测量值作为独占耗时。只相加独占时间。逐网表用同样的嵌套扣除，保留慢网 ID、两侧网名、真实 Pin 数、封装 DBU 包围盒、最终 HBT 数和重试次数。当前 DP 峰值记录单个活跃状态桶的最大标签数。

| AES 模块 | inclusive s | exclusive s |
|---|---:|---:|
| HBT 搜索 | 29.894 | 16.227 |
| 资源摘要 | 15.852 | 15.852 |
| Pattern DP | 48.809 | 11.765 |
| 资源可用性检查 | 26.399 | 7.816 |
| 路由验证 | 7.768 | 4.911 |
| 静态容量查询 | 4.301 | 4.301 |
| 主问题 | 18.695 | 3.509 |
| Commit | 11.496 | 2.736 |
| Release | 2.249 | 2.249 |
| 初始解 | 1.446 | 0.046 |

AES 在计时版中累计做了 1,874,469 次资源摘要计算、88,320,078 次容量查询和 173,445 次提交。最慢网 `_00002_` 仅有两个真实 Pin，却耗时 32.926 s。Stage 1 和 Stage 2 都在这根网内超时，分别只尝试了极少 DP 合并。该证据支持优先降低静态资源查询和重复提交成本，再避免按网 ID 从头耗尽阶段预算。[AES 逐网剖析](../../result-3/timeopt/_profiles/baseline/aes/slow_nets.tsv)

七例原版重跑的进程耗时合计 2534.515 s。计时版覆盖 2553.647 s，HBT 搜索独占 1597.647 s，占 62.56%。其后为主问题自身 186.932 s（7.32%）、资源摘要 159.046 s（6.23%）、动态资源检查 106.453 s（4.17%）、树验证 85.179 s（3.34%）及模式 DP 自身 80.342 s（3.15%）。Ariane 的原版初解为 858.523 s，HBT 搜索独占 829.23 s。ethmac 的主问题自身占 154.91 s，主要开销发生在候选比较预处理。两种结果解释了为何只降低 DP 状态数效果有限。[七例基线进程](../../result-3/timeopt/_baseline/summary.tsv) · [完整性能表](../../result-3/timeopt/profile_baseline.tsv) · [逐网排行](../../result-3/timeopt/slow_nets_baseline.tsv)

## 算法实现

fast 保持统一资源图。Stage 1 局部初解由原 CUGR 模式 DP 生成，跨 Die 初解同时评价单站与 Prim 分簇的多站解。Stage 2 按资源压力、HBT 数和网络规模调度，先协调已有候选的冲突分量，再逐网生成新的三维候选。Stage 3 只处理未连通、高压力或前序生成困难的网。当前解中的其他网保持提交。单网或分组接受都使用固定基础目标。配置 `prefer_hbt_nonincrease` 可给已有可行解的优化施加 HBT 非增条件，失连救援仍可使用新增站点。

精确模式保留全树标签和有限候选池比较。fast DP 状态是逻辑树节点映射的 (Die、层、GCell) 与排序后的实际物理边集。子树标签 A 与 B 经一条路径 P 合并时，对 A∪B∪P 的边执行并集去重，再由统一账本进行容量检查。每条 Wire 唯一计费，普通 Via 项的金属占用依实际 Via 累加。标签按基础代价及压力、线长、Via、HBT 和站点多样性保留。中间标签仅对完全相同的边集执行精确去重，Beam 明确改变候选池。两 Pin 网无须建立中间 Steiner 状态，直接扫描真实接入点与相应模式。

单站 L1 区域由每轴加权中位数的两个端点构成。R-tree 以矩形和点为查询对象。初解找不到可用近邻时，将访问范围翻倍到完整阵列。只有等距可用站点可能改变原路径时才运行原先的全量查询。单跨模式的一条局部路径下界由两侧端点至各自着陆格中心的独立 L1 线长、必要换层与 HBT 基础成本相加。fast 在已有足够替代候选且超出局部下界余量时剪枝；联合拓扑仍允许未剪枝 exact 枚举。双 HBT 在实际拥塞或直连失败时查询两端附近的局部站点对。最终路由同时核验站点最小间距和两端合法的 landing 层。

资源倒排按整数 ID 汇总每根候选网对资源的最大需求，累计潜在超容量形成冲突超边。不同 HBT 的最小间距关系也连接相关网络。动态价格由快照中的预测需求更新，一次更新为 max(0, η(D−C))，D 来自每网候选的均匀混合，属于搜索引导。分量内撤销现有树并执行局部 B&B，外部占用固定。搜索下界是已选部分与剩余各网成本最小值之和。成功后仍逐树提交。局部求解达到时限时可保留已知的更好可行结果。大分量分块、候选内动态价格与 Beam 的组合为启发式。

原生迷宫对 (Die、层、GCell) 使用唯一节点编号。四十万个状态以下用代次数组，高密度输入用整数键稀疏工作区。优先搜索封装坐标局部窗口，窗口中没有找到线路时回到全窗口，再细化稀疏步长。完整回溯后的资源检查基于当前树与新增唯一边的差量。

候选并行只在撤销后的同一分量快照中进行。工作线程只读统一账本，FLUTE LUT 在启动线程初始化，所有 FLUTE 访问受同一互斥锁保护。工作线程各自构造候选，主线程完成选择与事务发布。大例多线程仍受串行初解、输入和提交限制。内置独占计时是调度线程的时间，工作线程的任务时间涵盖在等待所在的作用域。

### 复杂度与正确性边界

设第 d 颗 Die 的状态数为 N_d=L_d X_d Y_d，令 N 为两者之和。局部节点编号为 offset_d+(layer·X_d+x)·Y_d+y。Wire 用本值，Via 用 N 加本值，HBT 用 2N 加独立站点 ID。容量词典与外部占用使用同一编号，编号范围无交叠。逐轴轨道和 Via 落地需求缓存约占 O(Σ L_d(X_d+Y_d))。常用网格的 O(N) 直接容量表最多占 40 MiB，动态占用空间随实际资源数量增长。摘要建立对 m 条单位边累计最多 5m 项，复杂度 O(m log m)，共享边先用物理边并集去重。准备好的摘要提交按资源数线性预检与更新，原导线需求同步给 CUGR patch。

设逻辑 DP 的父状态数和子状态数为 A、B，保留宽度为 W，每对子状态有 K 条连接路径，合并单位边数为 M。单次子树合并需要 O(A B K W²) 次候选检查，每次按 O(M) 合并边并构造累计需求。候选生成以及多个终端接入点的组合仍会扩大 K。初始搜索的索引近邻查询随实际访问范围增长，等距回退或资源耗尽可访问整阵列。旧版精确模式保留完整标签，有指数空间开销。路由拓扑层面的 Beam 与估价剪枝只用于快速模式。

冲突图按候选资源条目排序和聚合，不建立全设计的两两邻接。设待协调组的候选数为 c₁ 到 cₖ，局部精确搜索最坏有 ∏cᵢ 个叶节点，超容量和站点间距会缩小实际搜索。目标求解的价差随固定外部占用和实际候选池解释。迷宫对每次端点连接使用优先队列，最坏为 O((V+E)log V)，并复用距离状态和前驱。局部窗口、候选顺序与保留的最短回溯链用于候选生成，完整树提交照常验证。

## 七例全局布线实测

七例使用一线程和统一参数。原版的实际进程墙钟从 2534.515 s 降至 470.529 s，为 5.387 倍加速和 81.44% 降幅。HBT 站点由 57459 减至 57314，普通 Via 从 4166247 减至 4153057，总全局线长由 11659499.145 µm 变为 11661185.445 µm（+0.0145%）。单例线长变化最大的是 BP（+0.0553%）。该表使用全局树的物理线长，详细布线另行统计。[正式总体结果](../../result-3/timeopt/summary.tsv) · [逐项对照](../../result-3/timeopt/comparison_native.tsv)

| 模块 | 原始 inclusive s | 优化 inclusive s |
|---|---:|---:|
| 初始可行解 | 1677.820 | 160.211 |
| HBT 搜索 | 1701.737 | 132.827 |
| 资源摘要 | 159.046 | 37.089 |
| 可用性检查 | 256.409 | 93.654 |
| 模式 DP | 322.865 | 168.608 |
| 主问题 | 352.023 | 3.776 |
| Sparse Maze | 14.915 | 17.161 |
| 提交 | 225.223 | 8.684 |
| 释放 | 57.479 | 2.296 |

原版大多在首个网的 HBT 路径枚举内超时，七例统计的有效 DP 合并只有 84 次。新版在相同全局阶段预算下生成约 335 万个中间标签，Beam 规约单桶峰值为 16。A* 扩展也由 176 万升为 484 万，因为更多网进入三维重布。可比较的加速来自减少每次站点搜索、资源摘要与全网重复提交开销，并改变阶段调度。HBT 计数统计访问的候选着陆，不测量 R-tree 内部遍历节点，全程计数因处理网数不同由 66.6 万升为 284.3 万。不同用途的 inclusive 时间存在嵌套关系。[时间分解](../../result-3/timeopt/runtime_breakdown.tsv)

正式七例共有 749239 根网进入并完成初始布线。Stage 2 局部优化构建了 6295 个分组，6294 个在局部时限内完成候选选择，99 根网接受分组调整。该阶段随后尝试 4375 根网，接受 898 根网的单独替换。Stage 3 尝试 3092 根网，接受 1720 根网的修改。相同网可在多个阶段再次调整。两阶段均以实际资源竞争核验全部被接受的候选。[AES 的阶段文件](../../result-3/timeopt/aes/iteration_statistics.tsv)

本机 H3D 发行二进制用同一组哈希校验后的 LEF/DEF 重跑，七例进程墙钟合计约 92.975 s。AES 中它报告 2254 个可用站点，当前显式阵列含 2601 个。不同的站点可用性以及论文的运行硬件，限制了 H3D 运行时间作为同协议对照的用途。论文表中的 WL 和 Via 与正式详细布线对照，局部测得的 H3D 全局 Wire 指标保留原归一化口径。[同机 H3D 日志](../../result-3/timeopt/_profiles/h3d_local/summary.tsv) · [H3D 论文](https://doi.org/10.1109/ICCAD66269.2025.11240944)

三次按同一配置独立运行的总进程墙钟分别为 470.529、485.796、504.768 s，中位数为 485.796 s。原版三次总墙钟为 2534.515、2512.672、2509.824 s，以双方中位数比计加速 5.17 倍，降幅 80.67%。原版第一轮程序内计时合计 2506.872 s，正式新版本为 443.927 s。峰值内存从 BP 的 3,082,108 KiB 降至 1,215,448 KiB。模块时间拆解使用对应的 C++ 计时文件。[新版重复运行](../../result-3/timeopt/repeat_summary.tsv) · [原版重复运行](../../result-3/timeopt/repeat_baseline_summary.tsv)

AES 参数实验扫描了 Beam 和 HBT 数量 4、8、16、32，局部精确分量上限 4、8、16，以及五种初解顺序。在该样本上，按跨 Die 局部范围排序得到 387 个 HBT 和约 134964.9 µm 线长，同时普通 Via 为 89392。统一正式配置使用 local-first，得到 389 个 HBT、135048.9 µm 及 86223 个普通 Via。不同候选调度存在多目标取舍。[参数数据](../../result-3/timeopt/parameter_summary.tsv) · [墙钟与线长/HBT 图](../../result-3/timeopt/performance.pdf)

单独关闭 HBT 索引后，Ariane 进程墙钟为 900.336 s，HBT 搜索独占时间为 826.717 s。正式配置分别为 122.289 s 和 40.829 s。关闭静态资源缓存时，AES 容量计算独占 5.848 s，正式配置为 1.956 s。由于阶段预算固定，节省的逐项查询时间也会转化为同一预算内更多候选，整例墙钟下降幅度可以小于模块耗时下降。[单模块消融](../../result-3/timeopt/ablation_summary.tsv)

| AES、ethmac、Ariane、BP 的四例合计 | 进程墙钟 s |
|---|---:|
| 正式配置 | 315.991 |
| 关闭静态资源缓存 | 326.615 |
| 关闭 HBT 索引 | 1546.571 |
| 关闭冲突组优化 | 313.113 |
| 关闭迷宫工作区复用 | 344.706 |
| 关闭局部路径缓存 | 348.759 |

关闭冲突组略减四例墙钟，BP 该变体使用 14814 个 HBT，正式配置使用 14689 个。固定基础目标在两变体间也会随候选分布变化。保留冲突组主要是为了协调站点与共享资源，是否带来净质量收益取决于设计及搜索覆盖率。[消融逐例数据](../../result-3/timeopt/ablation_summary.tsv)

在 AES、ethmac、Ariane 和 BP 上，请求 1、2、4、8、16 个候选工作线程的合计墙钟分别为 324.422、338.387、331.241、339.450、355.731 s。局部组最多八网，请求 16 个线程时实际并发峰值为 8。当前并行粒度只覆盖部分候选生成，初解、资源提交、输出和部分 FLUTE 工作仍串行。此实现的多线程实验未取得端到端加速，正式结果采用一线程。[并行原始数据](../../result-3/timeopt/scalability_summary.tsv)

## 详细布线验收与质量

七个正式 case 全部用该次 C++ 输出重新运行 TritonRoute。14 颗 Die 均达到 DRC 0，七例均通过独立导体几何与 HBT 联合连通性验证。全局树同时通过 C++ 资源提交和回退检查及独立 Python 路由树检查，实际 HBT、Via 违规和未连接网均为 0。源 LEF/DEF、网映射、权重、站点规则及全局搜索预算与运行前基准一致。[输入协议等价性](../../result-3/timeopt/protocol_validation.json) · [详细布线总表](../../result-3/timeoptdetail/summary.tsv) · [实际工具与文件哈希](../../result-3/timeoptdetail/run_provenance.json)

| 详细布线指标 | 原始 Native | TimeOpt | 变化 |
|---|---:|---:|---:|
| 总线长 µm | 12217434 | 12223929 | +0.0532% |
| 普通 Via | 5754505 | 5762902 | +0.1459% |
| HBT | 57459 | 57314 | −0.2524% |
| 上下 Die 各自的详细耗时之和 s | 15140.954 | 18426.767 | +21.70% |

单例对原 Native 的最大线长及 Via 增幅均出现在 tinyRocket，分别为 0.1263% 和 0.5117%。各 case 的 HBT 数都持平或下降。总详细线长和 Via 小幅增长，未达到两者同时不超过原版的优先目标。各侧详细耗时之和包含并行执行的时间区间，实际每个 case 的详细布线墙钟另有字段；全局布线的约五倍加速仅适用于全局阶段。对 H3D 论文的详细质量，七例合计线长、Via 和 HBT 分别低 3.106%、2.525%、27.127%。[Native 详细对照](../../result-3/timeoptdetail/comparison_native.tsv) · [论文与同机 H3D](../../result-3/timeoptdetail/comparison_h3d.tsv) · [综合对照](../../result-3/timeoptdetail/comparison_all.tsv)

## 现阶段边界

- 已验证的核心改动包括离散 HBT 空间查询、可重复使用的硬资源摘要及有限候选的局部联合选择。三项分别有开关、反例和实际消融。可进一步研究候选池搜索覆盖率与详细布线质量之间的关系。
- 高扇出初解沿用 Prim 分簇和局部分支修复。原始 Prim 的二次复杂度仍然存在。当前并行实现覆盖分量内网络候选生成，尚无 Steiner 子树级并行。资源定价在冲突分解时更新一次，迭代对偶优化和长线段迷宫压缩尚未实现。
- 完整标签的 exact 模式保留原有主问题参考流程，资源层缓存同样生效。全设计 exact 求解的预处理上限、工作量和状态增长仍需优化。fast 的局部搜索窗口、分组、标签宽度、站点候选和接受条件明确限制搜索域。
- 当前数据支持原生三维全局布线显著提速及物理合法性。下游普通 Via 和 Wall time 的变化说明，还可以在不改变工艺与详细布线参数的情况下改善 Guide 质量。

## 复现基线

```bash
cmake -S src -B build-timeopt -DCMAKE_BUILD_TYPE=Release -DCUGR_NATIVE_3D=ON
cmake --build build-timeopt -j8
ctest --test-dir build-timeopt --output-on-failure
python3 scripts/native_bench.py build-timeopt/route tests/native/benchmark-array.json /tmp/native-profile-run --seed-seconds 1200 --stage-seconds 15 --master-seconds 15 --threads 1 --profile
python3 scripts/native_profile.py /tmp/native-profile-run /tmp/native-profile-tables
```

## 复现正式运行

```bash
python3 scripts/native_bench.py build-timeopt/route tests/native/benchmark-array.json /tmp/timeopt-routes --options tests/native/timeopt-options.json --seed-seconds 1200 --stage-seconds 15 --master-seconds 15 --threads 1 --profile
python3 scripts/native_summary.py /tmp/timeopt-routes --replay build-timeopt/native_replay
python3 scripts/native_detail_batch.py /home/wanghh/h3d/OpenROAD/bazel-bin/openroad /tmp/timeopt-routes /tmp/timeopt-drt /home/wanghh/h3d/OpenROAD/h3d_drt_validation/run_002 --jobs 3 --threads 8
python3 scripts/native_summary.py /tmp/timeopt-routes --detail-dir /tmp/timeopt-drt
python3 scripts/timeopt_experiments.py build-timeopt/route tests/native/benchmark-array.json tests/native/timeopt-options.json /tmp/timeopt-parallel --family parallel --cases aes ethmac ariane bp
```

正式归档的每个 case 都写入原始配置、进程记录、运行日志、三个阶段、逐网剖析和资源回放。详细布线在新目录中从该次 Guide、HBT 与局部分量生成 DEF，保留两侧运行日志、实际导体审计和联合连通性证据。[正式参数](../../tests/native/timeopt-options.json) · [全局文件来源](../../result-3/timeopt/run_provenance.json) · [详细布线总表](../../result-3/timeoptdetail/summary.tsv)
