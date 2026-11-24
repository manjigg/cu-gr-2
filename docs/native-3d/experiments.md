# Native CUGR-3D 实验记录

七例在同一原生 C++ 可执行文件上完成全局布线，共 749,239 根全局网、2,362,583 个真实端点。逐边重放得到 11,659,499.145 µm 导线、4,166,247 个 Die 内普通 Via 和 57,459 个独立 HBT 站点。2,438 根网各自使用了多个 HBT。输入哈希、运行时间、两侧用量、物理站点表和求解器状态均可复核。[总体布线总表](../../result-3/native3d/summary.tsv) · [七例输入与阵列复核](../../result-3/native3d/protocol-replay.json)

## 实验条件

- 原始 CUGR 基线为 `3377382`。原生 C++ 实验路由代码为 `add9702`，Release 可执行文件 SHA-256 为 `dddc3d7cce3c2a4ede25ce0426f1516c039be7be89e95bc11441df835d988e7e`。[冻结基线](baseline.md) · [实验二进制校验值](../../result-3/native3d/binary.sha256)
- 单线程全局路由。可行初解预算 1200 s，每个阶段候选生成和候选选择各 15 s。时间预算按实际时钟检查，排序、释放状态、复验可行解也耗时。不同机器的限时运行可能探索不同数量的候选。[配置与命令](native.md)
- 原始 LEF/DEF 均来自仓库 benchmarks。七例使用仓库内冻结的键合阵列配置，单位为 2000 DBU/µm，XY 原点为 (3000, 3000) DBU，两方向 pitch（阵列周期）及最小中心间距均为 3 µm，层对 metal7/metal7，站点容量 1。七例允许站点的交叠区内无额外封装禁区。固定站点、禁区、镜像、上下异网格和多层对另有受控测试。[固定输入指纹与站点规则](../../tests/native/benchmark-array.json)
- 目标权重为每微米 Wire 1、每个 Via 1、每个 HBT 4、拥塞权重 1。这些是优化价格。长度按各侧实际 DBU 计算，HBT TSV 的每一行对应一个独立物理站点。[AES 实验配置](../../result-3/native3d/aes/manifest.json)

## 总体布线

| case | 跨 Die 网 | HBT 数 | 多 HBT 网 | 单网最多 HBT | 两 Die 总线长 µm | 普通 Via | 进程耗时 s |
|---|---:|---:|---:|---:|---:|---:|---:|
| aes | 381 | 390 | 4 | 4 | 135355.500 | 89391 | 74.866 |
| tinyRocket | 1940 | 1967 | 16 | 8 | 313286.610 | 152698 | 73.062 |
| ethmac | 3487 | 3779 | 128 | 11 | 376460.875 | 164038 | 233.165 |
| mor1kx | 2927 | 4260 | 702 | 8 | 1271086.890 | 397993 | 124.443 |
| jpegencode | 16001 | 17181 | 560 | 25 | 2382605.250 | 1068195 | 409.756 |
| ariane | 12718 | 15068 | 722 | 45 | 2946594.000 | 1019442 | 1083.005 |
| bp | 3615 | 14814 | 306 | 81 | 4234110.020 | 1274490 | 726.414 |

七例均有可行树，未连接网、模型内 Via 容量违规与 HBT 冲突均为 0。六例 Wire overflow 的原始值是 0，BP 为 8.88178419700125×10⁻¹⁶，低于账本的 10⁻¹² 浮点比较容差。将所有网逆序释放后，七例的剩余资源键数全为 0。每例目录下的 tree-replay.json 是从输入与输出独立回读的结果，ledger-replay.json 来自空图重建、提交和释放。[BP 资源回放](../../result-3/native3d/bp/ledger-replay.json) · [Ariane 全局树回读](../../result-3/native3d/ariane/tree-replay.json)

### 候选搜索的实际覆盖率

七例整体状态都为 `FEASIBLE_TIME_LIMIT`。在此预算下，Stage 1 和 Stage 2 各自都在第一个网的完整标签枚举期间用完时间，新增完整枚举网数均为 0。这两个阶段的主问题使用先前可行初解建立的候选池。Stage 3 新增完成的网数见下表。各例三个阶段报告的可行目标在本次运行中相同。大例质量数据主要反映可行初解和已有候选的选择；跨 Die DP、detour 能力由下方可受控求解的反例单独验证。

| case | Stage 3 完成枚举的网数 | 最终候选池大小 | 最终池主问题状态 | 池内相对 gap |
|---|---:|---:|---|---:|
| aes | 3 | 13544 | TIME_LIMIT | 3.2273e-5 |
| tinyRocket | 0 | 27633 | OPTIMAL | 0 |
| ethmac | 4 | 47859 | TIME_LIMIT | 1.4548e-7 |
| mor1kx | 0 | 59113 | OPTIMAL | 0 |
| jpegencode | 2 | 218288 | TIME_LIMIT | 3.0800e-7 |
| ariane | 2 | 164754 | TIME_LIMIT | 3.2850e-8 |
| bp | 7 | 268133 | TIME_LIMIT | 1.7829e-6 |

例如 AES 的可行目标为 232258.737536，同一候选池下界为 232251.241803。tinyRocket 和 mor1kx 的 0 gap 证明各自已生成池上的最优性。完整三维物理解空间的 gap 留空。逐网完整边集的严格等价合并能保护后续共享选择，同时在高扇出网产生大量互异状态。这是当前大例的主要枚举限制。[AES 阶段记录](../../result-3/native3d/aes/output/routing_statistics.json) · [复杂度与下界](native.md)

## 与原始二维模式对照

原版在每颗 Die 上独立运行，原始切分端口位置固定。原生模式使用同一 LEF/DEF，只有经映射确认的切分端口进入可移动 HBT 建模。表中两个线长数都是全局布线的 M2 pitch 归一化指标，每侧按原输出习惯作整数截断后求和。原版与新版保留的二维入口另作同问题回归，14 份 Guide 的 SHA-256 以及原版资源统计逐项相同。[原版逐 Die 日志与哈希](../../results-3/native-3d/M0/summary.tsv) · [当前原生二进制的二维回归](../../results-3/native-3d/native-cli-2d/comparison.tsv)

| case | 原 2D 两侧线长指标 | 原生 3D 线长指标 | 变化 | 原 2D 两侧普通 Via | 原生 3D 普通 Via |
|---|---:|---:|---:|---:|---:|
| aes | 715955 | 712397 | −0.50% | 110704 | 89391 |
| tinyRocket | 1589190 | 1648876 | +3.76% | 191485 | 152698 |
| ethmac | 1966143 | 1981372 | +0.77% | 204965 | 164038 |
| mor1kx | 7036099 | 6689930 | −4.92% | 501966 | 397993 |
| jpegencode | 12551345 | 12540026 | −0.09% | 1358081 | 1068195 |
| ariane | 15936932 | 15508389 | −2.69% | 1249554 | 1019442 |
| bp | 24378757 | 22284789 | −8.59% | 1650184 | 1274490 |

## 代价敏感性和反例

- 六端三簇网关闭所有可行初解和 Stage 2/3，仅用 FLUTE 投影及 Stage 1 联合 DP。两侧三对真实端点分别落在三个投影点，HBT 价格为 0.25。完整枚举选出 3 个 HBT、4.2 µm 和 0 个普通 Via，有限候选目标 4.95、gap=0。在三列的双 Die 网格上连接六个端点需至少五条单位连接，达到 4.2 µm 线长只容纳两段水平 Wire，另三个连接由三个独立 HBT 提供。该网写成四个局部连通分量，两侧详细布线 DRC=0。[Stage 1 求解记录](../../results-3/native-3d/three-cluster-regression/three-hbt/out/routing_statistics.json) · [三站点及两端分量](../../results-3/native-3d/three-cluster-regression/three-hbt/out/hbt_assignment.tsv) · [实测导体联合验证](../../results-3/native-3d/three-hbt-detail/verified_report.json)
- 四端双簇用例在 wire=via=1 时，HBT 价格 1 选择 2 个物理站点、4.2 µm 和 0 个 Via，价格 5 时选择 1 个站点、8.4 µm 和 0 个 Via。前者输出三个局部连通分量，两个 HBT 将它们联成一棵树，两侧 TritonRoute 和按导体分量的联合回放都通过。[受控实验数据](../../results-3/native-3d/validation-final/sensitivity.tsv) · [曲线](../../results-3/native-3d/validation-final/pareto.pdf) · [详细布线物理回放](../../results-3/native-3d/clusters-geometry/joint_geometry.json)
- 放置通道阻塞后，局部 Stage 1 模式池不可行，同一输入在 Stage 2 找到双 HBT 跨 Die 绕行。单独运行 Stage 3 也通过两个合法 HBT 完成同一网络。移除跨 Die 网的全部合法站点后返回不可行。测试还注入重复边、缺失跨接、错误 landing 坐标和断开的详细布线导体，验证各层检查能拒绝错误。[集成测试](../../tests/native/test_flow.py) · [CTest 日志](../../results-3/native-3d/replay-regression.log)
- AES 分别运行仅线长、仅 Via、仅 HBT 以及四组混合权重。混合权重中，HBT 价格 1、4、8、16 分别得到 (394, 135311.4)、(390, 135355.5)、(383, 135452.1)、(381, 135477.3)，括号内为物理 HBT 数和全局线长 µm。仅线长运行得到 511 个 HBT、135832.2 µm，在本次限时测量中被多个方案支配。图上的折线只连接已测量的非支配点。[全部七组设置](../../results-3/native-3d/native-weights/sensitivity.tsv) · [AES Pareto 图](../../results-3/native-3d/native-weights/pareto.pdf)

## 详细布线

各 case 的上下 Die 分别由 OpenROAD/TritonRoute 完成，14 颗 Die 的 DRC 均为 0。OpenDB 导出的实际导体分量通过几何检查，再按物理 HBT 两端端口联结。同一条全局网的所有真实 Pin 汇入同一导体分量，通过 HBT 联结后未发现断网或跨网混接。Ariane Bottom 中有四个实例名包含转义反斜线。保留原始 DEF 和两侧 routed.def，修复 DEF 与 OpenDB 端点身份的对应后，重导出并完成几何审计。[详细布线总表](<../../result-3/native3d detail/summary.tsv>) · [Ariane 联合几何复查](<../../result-3/native3d detail/ariane/verified_report.json>)

| case | Top 线长 µm | Bottom 线长 µm | 总线长 µm | 两侧普通 Via | 两侧 DRC | HBT 联合连通 |
|---|---:|---:|---:|---:|---:|---|
| aes | 86367 | 64770 | 151137 | 124857 | 0 | 通过 |
| tinyRocket | 203869 | 127823 | 331692 | 205182 | 0 | 通过 |
| ethmac | 229142 | 168726 | 397868 | 213515 | 0 | 通过 |
| mor1kx | 578086 | 749386 | 1327472 | 538343 | 0 | 通过 |
| jpegencode | 995672 | 1500132 | 2495804 | 1398898 | 0 | 通过 |
| ariane | 1602193 | 1488219 | 3090412 | 1365205 | 0 | 通过 |
| bp | 2213369 | 2209680 | 4423049 | 1908505 | 0 | 通过 |

## 结果复现

当前代码的七例实际输出归档在 `result-3/native3d` 和 `result-3/native3d detail`。每例包括两份 Guide、逐边树表、按物理 site ID 输出的 HBT TSV、局部分量、运行日志、资源回放和阶段求解证书。详细布线每侧另有 placed.def、routed.def、drc.rpt、router.log 和 OpenDB 导体几何证据。各目录的哈希记录将所有原始输入、当前路由二进制以及各 case 的路由和验收文件关联起来。[当前原生结果来源](../../result-3/native3d/run_provenance.json)

使用仓库内冻结的 benchmark-array.json 重跑 AES 后，top.guide、bottom.guide、route_edges.tsv、components.jsonl 和 hbt_assignment.tsv 五个文件与正式运行逐字节相同。[复现实验文件哈希](../../results-3/native-3d/protocol-aes/aes/reproduction.json)
