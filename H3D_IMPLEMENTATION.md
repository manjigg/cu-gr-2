# H3D 双 die 全局布线实现

本实现依据《H3D: Heterogeneous Resources Aware Global Router for Face-to-Face Bonded 3D ICs》
第 III-B 至 III-E 节，在 CUGR 2.0 中实现端到端双 die 全局布线。
默认 `paper` 模式按论文公开的 III-B～III-E 方法实现：随机叶根数量 DP、固定引脚截断的 BFS 森林、
公式 (6) 连续区域、八方向候选、公式 (7) DP、逆 DFS 子网分解及 CUGR 模式/迷宫布线。
全合法域精确 L1 DP、跨网站点预算及强制单站点后备保留在显式 `enhanced` 模式。
原始方法审计见 `eval/h3d_aes/run_003/REPORT.md`；最新等价解优化和真实 AES 结果见 `eval/h3d_aes/run_005/REPORT.md`。
作者提供的七个原生 benchmark 与二进制对照见 **`作者七例复现对比.md`** 和
`eval/h3d_author/run_002/REPORT.md`。本轮修正了 III-E 子网接入：paper 默认在保持
子网端点/HBT 不变的情况下，使用实际引脚形状重新选择合法接入点并构造单 die RSMT，
不再强制沿用 HBT 重定位前的 Steiner 拓扑。新增配置见下表，历史观测结果独立保留。
失败根因的单变量对照见 **`eval/h3d_aes/run_006/REPORT.md`**：取消候选裁剪仍会耗尽站点；
同一 3 µm 数据存在已验证的全量解，但单次顺序局部最优策略会提前用完资源。
保持论文算法、显式改用 2 µm 间距的对照全量通过，不能算作原 3 µm 配置的修复。
随后沿原服务器路径找到了真实源文件，完成验证见 **`eval/h3d_aes/run_007/REPORT.md`**。
实际输入为 1 µm TerminalSize、1 µm TerminalSpacing；源规格分支用 1 µm pad、2 µm 阵列按论文单次流程完成全部 14310 网、两 die 零溢出。
双 DEF、LEF、跨 die 网表和映射与原提供文件逐字节一致，改正的是资源模型适配；旧 3 µm 条件仍单独保留。

## 1. 构建与运行

在项目根目录：

```bash
cmake -S src -B build -DCMAKE_BUILD_TYPE=Release -DCUGR_BUILD_TESTS=ON
cmake --build build --parallel 2
python3 tests/h3d/check_end_to_end.py
```

最后一条命令运行基准样例、同网 HBT 合并、纯单 die 网、拥塞重布、资源耗尽等场景，
并检查输出 DEF 的连接关系、guide、可重复性及 DEF 回读。默认结果目录为 `build/h3d_e2e_check/`。
测试程序自动以 `flute/` 为工作目录，使用版本管理中的查找表。

直接运行 H3D 时，在包含 `POWV9.dat`、`POST9.dat` 的目录执行。例如在 `run/`：

```bash
../build/route \
  -lef ../tests/data/h3d/tiny/technology.lef \
  -top-def ../tests/data/h3d/tiny/top.def \
  -bottom-def ../tests/data/h3d/tiny/bottom.def \
  -hbt-config ../tests/data/h3d/tiny/hbt.json \
  -h3d-method paper -h3d-seed 0 \
  -output-dir ../build/h3d_result
```

`scripts/build.py -o release` 会准备 `run/` 及查找表。
原有单 die 命令仍为 `route -lef ... -def ... -output ...`。
H3D 模式和单 die 模式不能混用 DEF 参数。

### 方法配置

| 配置 | `paper`（默认） | `enhanced`（显式启用） |
| --- | --- | --- |
| 数量 DP 根 | 从 degree-one 节点随机选择 | 首个 degree-one 节点 |
| 单 die 原始网 | 直接进入该 die 的模式布线输入，无跨 die DP | 保留历史处理路径 |
| 位置候选 | 连续区域内合法点与八方向最近点的并集 | 全部合法点，L1 距离变换 |
| 连续区域和 BFS 森林 | 公式 (6)，随机可移动根，遇固定引脚停止 | 固定引脚处分森林，无候选裁剪 |
| 后续网站点预留 | 无 | 至少为每条后续跨 die 网预留一个点 |
| 强制单站点后备 | 无 | 超预算时调用单站点精确解 |
| 拆分 | 逆 DFS，在 HBT 两侧按拓扑分量切开 | 保留历史按坐标合并路径 |

`-h3d-method paper|enhanced` 选择配置，`-h3d-seed N` 是 uint32 随机种子（默认 0）。
`-h3d-paper-ties coalesce|first` 控制位置回溯平局规则，默认 `coalesce`；`first` 恢复按候选编号首选。
`-h3d-assignment-trials N` 控制数量 DP 等价回溯的最大采样次数，范围 1～1024，默认 1。
大于 1 是显式的等价解搜索扩展，每个尝试仍保持原 mhc 最优值和原位置算法；它不是论文规定的单次求解流程。
报告同时记录 `method_profile`、`random_seed`、`method_features`，逐网记录根与位置随机种子。
论文模式耗尽 HBT 时返回非零退出码，报告失败位置及已完成范围。

论文没有规定随机数发生器、精确随机序列或所有平局规则。本实现使用 `mt19937`，
对每个方向保留全部 L1 距离并列最近点，基础等价成本按排序候选编号选择，再按下述零代价合并规则优化回溯。
这些是明确的工程约定；目前没有作者源码可用于逐行或随机序列等价验证。

## 2. 输入约定

输入为共用 LEF、Top DEF、Bottom DEF 和 JSON HBT 配置。
当前两个 die 必须使用相同的 DBU、裸片边界和 GCell 边界，并已经对齐到同一个键合平面坐标系。
LEF 与 DEF 的 DBU 也须相等。`top_transform` / `bottom_transform` 当前支持 `identity`；
需要镜像的原始布局应先完成坐标与朝向转换。

默认把两份 DEF 中同名的普通信号网归为同一条原始网。
同名实例和端口始终通过 die 身份区分。电源、地及特殊布线不会作为跨 die 信号网重新分配。
若两个 DEF 的本地网名需要映射，可在 HBT 配置中添加：

```json
{
  "net_aliases": {
    "top": {"local_top_name": "original_global_name"},
    "bottom": {"local_bottom_name": "original_global_name"}
  }
}
```

所有同名或别名映射必须反映真实的原始网表。不要把未连接的同名本地网误合并。
输入中的跨 die 网应保留实际端点，不需要预放 HBT 虚拟端口。

### HBT 配置

完整样例见 `tests/data/h3d/tiny/hbt.json`：

| 字段 | 含义 |
| --- | --- |
| `schema_version` | 当前为 1 |
| `dbu_per_micron` | 与输入 LEF/DEF 一致的 DBU 比例 |
| `coordinate_frame` | `bonded_xy` |
| `origin_dbu`、`pitch_dbu` | 二维站点阵列的起点和间距，单位 DBU |
| `site_counts` | x/y 两轴站点数量 |
| `pad_size_dbu` | HBT 终端矩形的 x/y 尺寸 |
| `spacing_dbu` | 可选的 HBT 最小边缘间距；还需满足两个键合层的默认间距 |
| `landing_layers` | 每个 die 的最高布线层名称 |
| `blocked_sites` | 禁用的 `[ix,iy]` 站点索引，从 0 开始 |
| `net_aliases` | 可选，本地网名到原始网名的映射 |
| `hbt_optimization_coordinates` | `physical`（默认）或 `gcell_centers`；后者按焊盘覆盖范围选择最近 GCell 代表点用于优化，物理 HBT 坐标另存；默认要求投影一一对应 |
| `allow_coincident_hbt_grid_points` | 默认 false；显式 true 时允许不同物理 HBT 拥有相同 GCell 代表点，保留独立候选 ID、物理位置和 owner，回溯按选中站点 ID 输出，适用于小于 GCell 间距的更密 HBT 阵列 |
| `subnet_routing_topology` | paper 默认 `flute`，对拆分后的实际端点重建单 die RSMT；`inherited` 保留旧拓扑接入，用于控制实验（enhanced 仍默认 inherited） |
| `subnet_pin_access` | `flute` 默认 `physical_shapes`，按真实引脚形状生成 CUGR 接入候选；`fixed_projection` 保留旧的原网投影接入方式 |
| `write_routing_traces` | 默认 `true`，输出逐网算法 trace；`false` 省略 trace/逐网日志，并紧凑输出汇总 JSON。完整和精简模式的双 DEF/Guide 已逐字节核对一致 |

站点编号为 `iy * nx + ix`。真实物理位置为 `origin + (ix,iy) * pitch`。
程序还会按两个 die 的键合层固定障碍和默认间距屏蔽受影响站点。
HBT 位置和身份始终使用真实站点坐标；映射到 GCell 仅用于后续全局布线。

## 3. 代码和论文的对应

| 模块 | 实现 |
| --- | --- |
| `src/obj/DesignInput.*` | 独立 Rsyn 逻辑/物理上下文，避免双 die 污染全局 Session |
| `src/h3d/H3DDesign.*` | 双 die 数据、网名合并、固定接入点及 FLUTE 平面拓扑 |
| `src/h3d/H3DTree.*` | 带 die 身份的引脚及可重新选根的无向树 |
| `src/h3d/DieAssignment.*` | III-B，公式 (3)(4)，逆 DFS HBT 数量 DP 和回溯 |
| `src/h3d/HbtGrid.*` | III-C，四叉树、区域查询、八方向最近站点和独占资源 |
| `src/h3d/HbtLocationOptimizer.*` | `runPaper`：公式 (6)、Algorithm 1 与公式 (7)；增强入口：全域 L1、单站点及预算 |
| `src/h3d/SubnetPartition.*` | III-D，逆 DFS 分解及同网同站点 HBT 别名合并 |
| `src/h3d/SubnetDecomposer.*` | 将各拓扑分量转换为带固定终端的单 die 路由输入 |
| `src/h3d/H3DGlobalRouter.*` | 原始网 HPWL 顺序调度、即时提交子网布线和最终连通性验证 |
| `src/h3d/H3DWriter.*` | 更新信号 NETS/PINS，保留原 DEF 其他段，并写出真实 HBT 终端 |
| `src/gr/PatternRoute.*`、`MazeRoute.*` | III-E，已有 DAG 模式布线、绕行及稀疏图拥塞重布 |

### HBT 数量 DP

`mhc(v,d)` 表示父边在 die `d` 时，节点 `v` 子树的最少拓扑 HBT 数量。
枚举节点的 die 区间，覆盖固定引脚和父边，并让每个孩子在区间内选择最低成本状态。
区间跨度是该节点的 HBT 成本，因此多个异 die 分支在同一节点只计一个 HBT。
两 die 情形每节点只有三个区间，时间和内存为 O(|V|)。
最优性针对给定的 FLUTE 拓扑；它不是任意拓扑下的全局 HBT 最少化。

### 论文模式：HBT 位置 DP

`HbtLocationOptimizer::runPaper()` 从随机可移动节点建立 BFS 树，分支遇到固定引脚停止；
继续处理未覆盖的可移动节点，形成森林。固定引脚可能同时是不同树的边界叶子。
位于原始引脚位置的 HBT 被展开为可移动 HBT 与各 die 的固定引脚叶子。

逆 BFS 中，对孩子连续区间的全部端点排序，取两个中位端点作为本节点 x/y 最优区间，形成 `Bv`。
候选集合为 `Sc_v = (Bv ∩ Sv) ∪ Dv`：`Sv` 对 HBT 是当前空闲站点，对普通 Steiner 点是 routing 网格；
`Dv` 是八个方向的全部并列最近合法点。候选排序去重后，逐候选、逐孩子枚举公式 (7) 成本与最优前驱，
再从各树根正向回溯。整网评分期间不占站点，回溯后才统一提交同网使用的不同站点。
该模式不调用全域距离变换、未来网站点预留或强制单站点后备。

`paper_location` 保存区域、候选编号、完整成本/前驱表、选中状态、BFS 顺序和连续解。
`scripts/validate_h3d_paper.py` 用平面扫描查询、直接枚举转移和独立图遍历验证这些观测。
验证结论是论文候选集上的 DP 一致性；受限候选集与所有合法点的最优值可能不同。

#### 等成本回溯优化（默认开启）

先得到原始首选回溯和完整成本表，再枚举所有 HBT 候选集合的交集；若能在同一候选点共点，
且每棵优化树的最小成本与原公式 (7) 完全相同，则选这个等价解。
若无法全部共点，继续尝试把两个现有站点组并到其候选交集中的点。
每次必须严格减少实际占用站点数，且不增加当前分配、当前候选域上的最小线长。
两组尝试每网最多 256 次，这是计算量上限，不是 HBT 资源预算；不保证找到最少物理站点的所有等价解。

完整原成本表保持不变，只更新实际回溯路径上的等成本前驱。
`tie_merges` 记录每次合并的节点、站点及候选下标，独立检查重新计算每一步的全树线长和站点数。
`first` 禁用以上优化，可复现 run_003 的处理数和失败位置。

#### 数量 DP 等价回溯搜索（显式可选）

`-h3d-assignment-trials 32` 在原始回溯之后，对数量 DP 的等成本区间/孩子状态做固定种子采样，
保留相同最小拓扑 HBT 数量；对相同 HBT 节点集合去重，每种新集合仍调用 `runPaper()`。
在实际计算的解中，先选物理站点更少者，再选位置线长更短者；初始方案只用一个站点时直接保留。
候选集构造、公式 (7) 和站点提交规则不变，没有未来网站点预留或强制单站点后备。

该扩展可能改变数量回溯，因而不同回溯间的位置线长可能增加。
它与“同一个回溯下零线长增量的合并”不同；报告分别记录搜索次数、选中回溯以及线长取舍。
不宣称穷举全部最小数量解，或达到跨网全局最优。

### 增强模式：全域 HBT 位置 DP

在固定引脚处切分成森林，BFS 建立顺序，再逆序处理子树。
固定引脚位于初始 HBT 节点时，先将其拆成可移动 HBT 和各 die 的固定引脚叶子。
每个 HBT 使用全部空闲合法站点，每个普通 Steiner 点使用完整的可用 routing 域。
对非固定孩子，在两种网格及固定引脚的坐标并集上做 x/y 正反向 L1 距离变换，
精确计算 `min_t(g_child(t)+|s-t|_1)`，同时传播用于回溯的最优合法种子编号。
不合法/已占用位置不是种子；坐标并集中的中间计算位置不增加任何 HBT 资源。

无物理站点数量预算时，该方法在给定拓扑与离散域上精确求解公式 (7)，
避免旧的八方向最近候选遗漏和候选两两比较。它不是任意拓扑下的全局 Steiner 最优算法。
状态量依赖合法域与坐标并集大小；不宣称候选数量是常数。

候选评分期间不占站点。整网回溯后，同网重复站点统一占用一次；其他网不能再使用。
若没有可用站点，程序返回非零退出码，不把普通 GCell 当作合法 HBT。

### 增强模式：跨网容量预算与精确单站点后备

设当前空闲站点数为 F，当前网之后还有 R 条未处理跨 die 网，本网最多使用 `F-R` 个不同站点。
如果初始化就少于跨 die 网的一网一站点下界，程序明确报错。
无约束精确解满足预算时直接采用；若超预算，求同网全部拓扑 HBT 共用一个合法站点的最优方案。
预算为 1 时直接进入单站点求解。

单站点求解对每个可能的公共 x/y 坐标分别做一维树 DP，最后只在实际可用二维站点上组合成本。
普通 routing 域为完整笛卡尔积时，此分离是精确的；有洞时逐个公共站点调用完整二维解。
因此单站点条件下的位置和普通 Steiner 点都得到精确最优值。
每次提交后检查至少还剩 R 个站点，从而防止前序网络耗尽后续网的最低站点需求。
这是保证当前抽象站点模型可继续的容量感知顺序策略；一般 K>1 的基数约束和跨网全局线长不宣称最优。

### 子网与资源提交

拓扑边拥有独立的 die 标签。在每个 HBT 两侧建立终端，按每个 die 内的连通分量生成子网。
论文模式在逆 DFS 中连接同 die 边，只将同网同站点 HBT 作为别名合并；普通节点坐标相同不会连接不同拓扑分量。
同网 HBT 合并后消除分量内部的重合边和冗余环路。
每个分量映射到 GCell 后在分量内部合并重合节点，同时保留所有固定金属层约束。
paper 默认把子网实际引脚交给 CUGR 重新选择接入点、构造 RSMT，再进入已有 DAG 算法；
显式 `subnet_routing_topology=inherited` 才通过 `PatternRoute::setSteinerTree()` 沿用旧拓扑。
原始网按物理 HPWL 处理，完成子网布线后立即提交需求；最后运行溢出网的绕行/迷宫修复。
修复只改变各 die 内路径，HBT 端点和所属子网保持固定。

全部 HBT pad 建立后、拥塞修复前，调用与初次建图共用的 `GridGraph::rebuildCapacities()`，
按最终固定几何重新计算容量，但保留已提交的需求和路径。
逐边核对需求未变、容量没有增加，并与从零构建的最终几何地图对照。
之后 Pattern/Maze 使用包含全部 pad 的正确容量图修复溢出。
报告的 `pad_capacity_sync` 保存变更边数、容量扣减和两项一致性检查结果。

## 4. 输出和指标

输出目录包含：

```text
top.def
bottom.def
top.guide
bottom.guide
h3d_report.json
```

DEF 是更新了网表和 HBT 终端的布局输入，guide 是全局布线结果。
`h3d_report.json` 保存原始网/子网/HBT 映射、站点占用、候选数量及每 die 的线长、普通 via 数、溢出网数。
`topological_hbt_count` 是位置优化前的拓扑 HBT 数；`physical_hbt_count` 是合并后实际占用站点数。
普通 via 数不包含 HBT 数。

`optimized_tree_wirelength_dbu` 是位置 DP 的拓扑 L1 长度；`routing.*.wirelength_dbu` 是最终 GCell 布线树长度。
二者和详细布线后的实际金属线长分别属于不同阶段，不应混用。
终端 pad 由输出 DEF 明确表示，最终制造规则和详细可布性需用真实工艺和详细布线器验证。

## 5. 已有验证

```bash
ctest --test-dir build --output-on-failure
```

六组测试：

1. **die assignment**：小树穷举所有边的 die 分配，检查每种根、镜像 die 平局规则和固定种子平局规则的最优值及回溯；包含高扇出和一万节点链。
2. **HBT optimization**：四叉树与暴力查询对照、64 位 DBU、原子占用/释放、80 组多变量/非均匀网格/空洞穷举、单站点与站点预算反例。
3. **paper method**：随机叶根、方向等距点、连续区域、候选/公式 (7)、固定点截断 BFS 森林及逆 DFS 分解；包括防止普通坐标重合连接不同分量的反例。
   还验证受限候选成本 60、全域增强成本 54 的反例，防止论文入口混入全域优化。
   新增零代价全部共点、三站点到两站点局部合并、拒绝有线长代价的合并，以及数量等价回溯搜索的确定性/选择规则。
4. **input provenance**：HBT 间距必须来自实际 placer 输入或显式参数，核对单位和 die 边界，排除落在上边界、无法容纳正尺寸 pad 的中心点。
5. **single die fixture**：原有单 die 输入和 guide 输出回归。
6. **end to end**：显式 `enhanced` 模式，独立读取输出 DEF，重建跨 die 连通关系、检查无串网和端点丢失、HBT 合法性、两份 DEF 回读、确定性、合并和耗尽场景。
   还包括两条跨 die 网共享两个站点预算、pad 容量同步、nested PORT、无 PINS 段，以及拒绝不满足 pad 间距的配置。
   低容量变体会实际触发第二阶段绕行和第三阶段迷宫重布；该变体允许保留溢出，用于验证重布路径和连通性。

增强模式小样例基准结果：6 条原始网，3 条跨 die 网，4 个物理 HBT；Top 6 条子网、Bottom 4 条子网，两个 die 都无溢出。
同网合并变体将两个拓扑 HBT 合并为一个物理站点。详细结果由每次测试重新生成。
这些检查验证算法与全局流程，不代表已经复现论文在真实基准上的质量/运行时间指标。

算法模块还可单独使用 AddressSanitizer/UBSan 检查，命令见 `tests/h3d/README.md`。

新增资源反例遵循 HPWL 升序：第一条网使用两个站点的唯一局部最短解会耗尽第二条网的资源；
即使保留全部合法候选，结果也相同。共同站点可行解需要放弃第一条网的局部最短线长。
`runPaperFullDomainAudit()` 仅用于该单变量诊断：同一 BFS 根/森林、同一公式 (7)，仅取消候选裁剪；不作为论文生产入口。

## 6. 真实 AES 输入适配和诊断入口

首次真实输入实验保留在 `eval/h3d_aes/run_001/REPORT.md`，该历史版本因顺序分配耗尽站点而失败。
增强模式的历史全量结果见 `eval/h3d_aes/run_002/REPORT.md` 和同目录 `summary.json`：
14310 条原始网、1679 条跨 die 网全部完成，同一 1764 站点阵列下两边 Overflow 为 0，端点/连接独立验证通过。

原始首选平局规则的论文模式结果见 `eval/h3d_aes/run_003/REPORT.md`：seed 0 完成 13635 条原始网、1392 条跨 die 网后，
1764 点耗尽，在 `u0.tmp_w\[19\]` 的位置 DP 失败；还有 675 条原始网（287 条跨 die）未完成。
全部 14310 条固定拓扑的数量最优值和原始端点已独立核对；已完成前缀的 3761 份连续区域/DP 表、31500 项候选成本、
15388 个分解分量与独立参考一致。该前缀单独完成容量同步、拥塞修复、输出验证和原生 DEF 回读，保留全部 51663 原始端点。
前缀输出不是全设计成功；重复全量运行的失败位置、完成数和 trace SHA256 相同。

最新结果见 **`eval/h3d_aes/run_005/REPORT.md`**：

| seed 0 配置 | 完成原始网 | 完成跨 die 网 |
| --- | --- | --- |
| `first`，数量回溯 1 次 | 13635 | 1392 |
| 默认 `coalesce`，数量回溯 1 次 | 13760 | 1431 |
| `coalesce`，显式数量回溯上限 32 次 | 13908 | 1482 |

32 次配置比原始规则多完成 273 条原始网、90 条跨 die 网，仍在 `text_out[8]` 处耗尽 1764 点。
其完成前缀通过 4273 份区域/位置 DP 表、36056 项候选成本、15662 个拆分分量的独立核对，
2785 个数量回溯的边标签均达到原 mhc 最小值。
26 条网在不同数量回溯间以更长位置线长换取更少物理站点；各自回溯内部的公式 (7) 最优值保持正确。
该前缀已完成最终容量同步、零溢出输出和原生 DEF 回读，仍保留 402 条未处理网及全部原始端点。

新增输入配置：

- `routing_layers`：显式启用 LEF 金属层的连续前缀。例如 AES 的 LEF 有 10 层，DEF 只提供 metal1–metal6 轨道，实际只启用这 6 层。
- `gcell_pitch_dbu`：DEF 没有 GCELLGRID 时使用的物理网格间距；AES 采用原建议值 4200 DBU。
- `-3d-nets PATH`：读取 `nets[].original_net_name/top_pins/bottom_pins` 格式，严格核对全部跨 die 网络的端点多重集合。
  `prior_terminal_reference_dbu` 不作为位置约束。
- `-h3d-init-only`：完整加载双 die、容量网格和所有网络，导出 `input_statistics.json`；不需要猜测未提供的 pad 几何。
- `-h3d-topology-only`：运行现有 FLUTE、HBT 数量 DP 并导出拓扑和回溯状态，不分配 HBT 物理站点。
- `-net-selection PATH`：显式 JSON 网名数组，用于诊断或部分网络实验。始终加载完整设计；部分路由输出会保留未处理的原 NETS 和端点。

默认不选择子集时仍处理全部网络。部分实验的报告包含 `experiment_scope` 和 `selected_net_count`，
其 guide 只覆盖被实际处理的子网，不代表保留在 DEF 中的其他网已布线。
新报告还导出阶段计时、站点初始状态、逐网观测和失败快照；这些只用于观测，不改变核心代价或 DP 转移。

AES 原始 HBT 配置没有 pad 尺寸。该次实验在用户明确授权后采用 0.14×0.14 µm 的试验 pad，
记录在独立派生配置中，不是原始工艺 HBT 几何，也没有通过详细布线或 DRC。
增强模式曾在原占用快照上修复 20 个位置反例，run_002 的 934 个单 HBT 全站点枚举均无差距。
论文模式 run_003 前缀的 738 个单 HBT 全站点枚举中仍有 3 个差距，最大 73800 DBU；
独立审计确认其公式 (7) 与论文候选集一致，因此不把它们描述成全合法域精确最优。
run_005 的 32 次配置中，768 个单 HBT 全站点枚举有 1 个差距（6000 DBU）；不同运行的占用快照不同，不能据此宣称消除了所有候选裁剪问题。
这些结果是现有试验几何/全局容量模型下的验证；真实工艺、合法化、详细布线与 DRC 仍是后续阶段。

### 资源失败与输入来源的修正

`run_006` 保持首选平局、数量回溯 1 次，以排除等价解扩展的影响。
3 µm 八方向版本完成 1392 条跨 die 网，全域同森林对照完成 1391 条，均在空闲站点为 0 时失败。
去掉 187 条含 I/O 的跨 die 网后，1386/1492 条完成，仍耗尽。
因此八方向裁剪和 I/O 增量均不是此失败的必要条件；同一数据/同一 3 µm 站点的历史完整输出重新验证通过，排除了全局模型下必然无解的解释。

原生报告现记录 `failure_category=hbt_resource_exhaustion`、`hbt_free_sites_at_failure` 和 `hbt_resource_audit.first_overcommitment`，
区分站点用尽与候选查询错误、金属路径布不通。首次资源承诺超支只用于观测，不改变论文分配决策。

原始包的转换脚本把 3 µm 写成研究假设，未从 placer 读取；当前转换器已取消隐式默认：

```bash
python3 benchmarks/h3d_aes_convert.py --root /path/to/Place-LoL --out /path/to/new_input \
  --placer-input /path/to/Place-LoL/binaries/converted_input/default/aes.input
# 或明确指定研究间距，输出会标记为覆盖值：
python3 benchmarks/h3d_aes_convert.py --root /path/to/Place-LoL --out /path/to/new_input \
  --hbt-pitch-um 3.0
```

默认查找上述 placer `.input`；读取 TerminalSize、TerminalSpacing、DieSize，核对 DBU 与边界。
允许 placer 的行式 placement core 被包含在 DEF 外框内，不把两者正常的尺寸差当成格式错误。
从源模型传递 pad 尺寸、边缘间距，并由二者推导最小中心间距；规则阵列原点仍是明确记录的建模约定，不自动认证真实工艺。
缺失源规格时必须显式给出间距。现有九个 AES 输入文件保持原样。
I/O 逐网检查进一步确认：187 条含 I/O 跨 die 网中，186 条仅由 Bottom 端口约定造成跨 die，1 条即使去掉端口也有内部跨 die 端点。

源文件修正分支的实际命令（需新输出目录，示例为已运行目录）：

```bash
python3 scripts/rebuild_h3d_aes_from_source.py \
  --source-root /home/wanghh/Archived/Open3DBench/Place-LoL --out eval/h3d_aes/run_007
python3 scripts/h3d_aes_experiment.py run --experiment eval/h3d_aes/run_007/experiment \
  --stage full --method paper --paper-ties first --assignment-trials 1 --seed 0 --timeout 600
```

`prepare --source PATH` 支持独立转换输入，并在各实验的 `input_audit.json` 中记录来源；运行、回读、独立检查使用该来源，历史实验仍使用原目录。
