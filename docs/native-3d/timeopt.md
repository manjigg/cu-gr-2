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

本机 H3D 发行二进制用同一组哈希校验后的 LEF/DEF 重跑，七例进程墙钟合计约 92.975 s。AES 中它报告 2254 个可用站点，当前显式阵列含 2601 个。不同的站点可用性以及论文的运行硬件，限制了 H3D 运行时间作为同协议对照的用途。论文表中的 WL 和 Via 与正式详细布线对照，局部测得的 H3D 全局 Wire 指标保留原归一化口径。[同机 H3D 日志](../../result-3/timeopt/_profiles/h3d_local/summary.tsv) · [H3D 论文](https://doi.org/10.1109/ICCAD66269.2025.11240944)

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
