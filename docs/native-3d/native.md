# Native CUGR-3D 开发与使用记录

开发起点为本地 `2d-baseline` 的 `33773823b543dd4b2a2f6f5a859b2e614b9dc4c2`，开发分支为 `native-3d-f2f`。原版读取、模式布线、拥塞绕行、迷宫、资源提交和 Guide 输出的逐函数审计见 [baseline.md](baseline.md)。实验分支的 C++ 补丁没有参与构建。

## 编译与运行

在仓库根目录执行。项目使用 C++14、Linux/POSIX、原仓库的 LEF/DEF 库和 FLUTE 表。原生模式补充使用已安装的 Boost.Serialization（Boost Software License 1.0）。有限候选主问题的分支定界器在 C++ 内实现，无需安装 MILP 软件。

```bash
cmake -S src -B /tmp/native-cugr-check -DCMAKE_BUILD_TYPE=Release -DCUGR_NATIVE_3D=ON
cmake --build /tmp/native-cugr-check -j8
ctest --test-dir /tmp/native-cugr-check --output-on-failure
/tmp/native-cugr-check/route -inspect-native tests/native/aes-input.json
/tmp/native-cugr-check/route -native tests/native/clusters/manifest.json -output /tmp/native-clusters-demo
/tmp/native-cugr-check/route -check-native tests/native/clusters/manifest.json -output /tmp/native-connectivity-demo.json
python3 scripts/native_check.py tests/native/clusters/manifest.json /tmp/native-clusters-demo
```

路由输出目录须为空，检查报告须使用新文件名。原生入口按当前工作目录、`CUGR_FLUTE_LUT` 或编译时记录的原始表目录查找 POWV9.dat 和 POST9.dat。纯二维入口沿用原版运行目录要求，可用 `-DCUGR_NATIVE_3D=OFF` 独立构建。原始运行命令的七例实例见实验目录 M0/run.py。

## 输入及物理单位

可运行配置位于 [四端点的双 Die 配置](../../tests/native/clusters/manifest.json) 和 [镜像异网格配置](../../tests/native/dual.json)。同一配置中的 Top 为 Die 0，Bottom 为 Die 1。

| 配置 | 含义 |
|---|---|
| bonded_dbu | 每微米的封装 DBU 数，须为两侧局部 DBU/µm 的整数倍 |
| dies[].lef、def | 每侧的输入，允许绝对路径和相对于清单的路径 |
| transform | 每个封装轴 i 的换算为 offset[i] + signs[i] × scale × local[axes[i]]。axes 必须为 0/1 的排列，signs 为 ±1，offset 为封装 DBU |
| mapping | 一条记录对应一个显式的全局网，分别提供 top.net、bottom.net。可选 cut_port 要求真实存在且属于该局部网。未映射的同名网各自独立 |
| via_capacity | 可显式指定每格每相邻层对的容量，或选用 track_intersections，按两层的优选轨道在该格内的交点数建模。后者另受金属障碍和最小面积占用约束 |
| bonding | origin、pitch、min_spacing、keepouts 均为封装 DBU。当前 keepout 为禁止站点中心落入的闭矩形。不同站点间距按欧氏距离检查 |
| bonding.sites[] | id、xy、capacity、layers，以及可选 fixed_net。layers 是可用的两端金属层名对。同一物理站点只能归属于一个逻辑网 |
| bonding.l1_die_weights | 可选的两侧正整数权重，默认 [1,1] |
| weights | wire_um、via、hbt、congestion 均为非负系数。可选 hbt_pressure 单独定义 HBT 稀缺性价格，short_area_um2 选择以平方微米为单位的预计短路面积价格 |
| seed_seconds | 初始可行解的时间预算，默认 0。正式 benchmark 批处理显式启用。seed_site_seconds 控制初解中额外近邻站点试探的时间 |
| stage_seconds、master_seconds | 每阶段候选生成与有限候选主问题的独立时间预算。报告记录实际时间，包含标签释放、排序、事务预检等工作 |
| stages、sparse_stride | 可选的递增阶段序列和稀疏轴间隔。迷宫失败后逐级缩到 1。same_die_maze_detour 默认关闭 |
| landing_half_width_dbu | 派生详细布线 DEF 的方形 landing 半宽，单位为该 Die 的 DBU。七例还可从已确认的、大小一致的原始切分 PORT 形状推导 |

Rsyn::Session 的底层上下文是进程级状态。`Design::importDie` 在主线程依次启动一次性子进程，各自读取一个设计，转换为 CUGR 自有模型，序列化到匿名文件。父进程仅恢复值对象。两个模型各持有自身的局部格线、金属层、宏、实例和引脚。隔离测试分别交换读取顺序并从 DEF 独立计数。`routingBlockages` 补读 DEF BLOCKAGES 的矩形或正交多边形。遇到未支持的层或约束变体会报错。

七例输入转换器在先前实验明确启用 paired_ports 时才解释 T 编号。两侧同名端口、所属网名、每网切分端口唯一性都经过检查，原生 C++ 导入还会验证它们确实是该网的 PORT。其他 PORT 和所有实例端点继续保留。旧 cut_port 的原位置不参与新 HBT 选择。七例的参考阵列来自已保存的 f2f.json，pitch 与中心间距均为 6000 DBU，即 3 µm，原点 (3000,3000)，两端均为 metal7。这个参考栈使用恒等变换，镜像由单独测试覆盖。实验设置与代工厂键合规则的来源应分别记录。

## 数据结构与调用

| 文件和入口 | 基线行为 | 本次实现及原因 |
|---|---|---|
| src/global.h、src/main.cpp | 一份 LEF/DEF 进入 Design | 保留原参数，增加 -inspect-native、-native、-check-native 三个原生入口 |
| src/obj/Design.{h,cpp}、Instance、Layers、Net、GeoTypes | Rsyn 读取、对象转换，原有 net 端点依赖局部下标 | 保留端点名称和原始实例索引，记录 Via 层对和类型，支持自有值对象快照 |
| src/native/DesignImport.cpp、DefBlockages.cpp | 原程序仅初始化一次 Rsyn | 进程隔离与阻塞几何补读，避免第二份 DEF 覆盖第一份 |
| src/native/MultiDieDesign.{h,cpp} | 局部 net ID 仅在一份 DEF 中有效 | DieDesign 含独立 GridGraph，GlobalNet 保存双侧局部 ID 及每个 GlobalPin 的 (die,net,ordinal,original,mappingPort) |
| src/gr/GRTree、GRNet、GridGraph | 层相同推导 Wire，不同推导 Via | GRPoint 比较和四维哈希包含 Die，每个树子节点记录入边类型。原局部遍历遇到 HBT 会报错。二维 XY 哈希只在单颗 Die 的局部投影中使用 |
| src/native/RoutingGraph.{h,cpp} | 原资源提交按局部树修改 Wire Demand | 统一节点、单位边、物理 siteId、资源标识、可行性、带所有权的事务和撤销。把 Wire Demand 同步至原 GridGraph，供拥塞和 Guide patch 读取 |
| src/native/PatternSearch.cpp、MedianRegion.cpp、SeedTopology.cpp | 原始 FLUTE + XY 逻辑 DAG + 金属层 DP | 二维投影保留端点列表，Die/层/物理资源集进入树形 DP。另用实际局部布线评价单 HBT 和空间分簇的多 HBT 初解 |
| src/gr/PatternRoute.cpp | 原有 constructDetours、constructRoutingDAG、calculateRoutingCosts 和 run | 原生模式在同 Die 候选中调用原 DP 与 detour，保留 FLUTE C 实现和查找表，FLUTE 返回内存按生命周期释放 |
| src/native/SparseMaze.cpp | 原 MazeRoute 的逻辑稀疏状态是 XY、H/V | 按 Die 独立选轴，在真实层维度扩展合法 Wire/Via/HBT；多端点搜索从已连接树重新出发。回溯的 HBT 两端固定，同层线段交回模式 DP 枚举替代层 |
| src/gr/GlobalRouter.h、src/native/NativeRouter.cpp | 单 Die 三阶段共享一个 GridGraph | GlobalRouter::route 的原生重载共享同一 MultiDieDesign、RoutingGraph、候选池和所有权账本。各阶段将旧候选纳入新主问题 |
| src/gr/GlobalRouter.cpp、src/native/GuideOutput.cpp | 全树在单一网名下生成 Guide | 复用 componentGuides 的 Pin Access 与 Wire Segment patches。删除 HBT 边后以各局部连通分量单独命名并输出，额外覆盖各 HBT landing 格 |
| src/native/Reachability.cpp | 无独立静态物理图审计 | 放松竞争及固定归属后求连通分量。某网所有 Pin 的可达分量交集为空，即给出该离散模型的不可行证据。交集非空仅是必要条件 |

## 优化问题与证书

物理点为 (Die, 局部金属层, 列, 行)。Wire 的资源标识由 Die、金属层、低端 GCell 构成；Via 连接同一 Die、同一格的相邻合法层；HBT 以独立物理站点 ID 标识。同一格可承载多个站点。每条候选先按类型及端点去重，再加入 Via 对上下金属相邻边的最小面积需求。各 Die 的 Wire 长度按自身 DBU 换算成微米。

1. `PatternSearch::enumerate` 把 Pin 的代表性格中心映射到 bonded XY，维护投影点到原始终端列表。FLUTE Branch 提供一个平面骨架。逻辑节点的标签以 (Die, layer, GCell, 已用边集) 标识，合并子分支取并集，再检查容量、固定站点和连通性。多 AP 的真实 Pin 在叶端保留可选接入点。
2. Stage 1 的每条跨 Die 逻辑连接遍历所有合法站点和层对，同时枚举两侧直线/L 路径及相邻 Via 栈。Stage 2 增加同 Die 逻辑连接的双 HBT 绕行，并将局部阻塞方向交给原 CUGR trunk/stem detour。现有候选族对单条逻辑分支的模式有限，整棵网可以包含多个跨 Die 分支。凸 L1 排名只改变完整 DP 的探索次序。
3. 加权 L1 连续最优集合是两个逐轴加权中位数区间的笛卡尔积。排序后累计权重 P，第一个满足 P ≥ W−P 的坐标为下端，第一个满足 P > W−P 的坐标为上端。初解在物理站点索引上取候选，并评价两侧 Wire/Via。分簇候选采用含 Die/层转移下界的 Prim 生成树，对跨 Die 分支插入独立合法站点，再对局部连通分量做 FLUTE 与模式布线。
4. 现阶段对中间 DP 标签使用保守的等价合并。只有根状态和完整类型化边集均相同时才删除重复标签。任何后续分支与这两个标签并集后的拓扑、需求、成本与回溯相同。中间边集不同，即便 HBT 数与当前成本相同也保留。到达已验证连接所有终端及固定站点的根后，若一个候选的类型化物理边集为另一个的真子集，则前者在每项非负资源需求与成本上均不较差，后者可从 master 的搜索域删除。其他网可用空间只会扩大。
5. 主问题对每网选择一个候选。目标是所有候选的固定价格之和，硬约束检查共享容量、site 所有权和间距。B&B 的下界是已选成本与各未选网独立最低成本之和。选择候选后事务提交，回退时撤销。可行上界来自已验证整组路由。若每根网已选候选都达到各自最低成本，即得当前候选池的最优性证书。运行超时保留已知可行解及候选池下界。
6. 候选价格包含物理线长、普通 Via、独立 HBT 和容量稀缺性。可选短路项按实际 Wire/Via 着陆需求、线宽、长度和冻结的 logistic 风险估计面积。固定价格使 master 的上下界独立于搜索顺序。基线 `GridGraph::getWireCost/getViaCost` 的动态拥塞成本仍用于原局部模式与绕行候选生成。原生 master 并未对所有线网联合求一个动态非线性短路面积极小值。
7. 迷宫按真实优选方向、普通 Via、合法 landing 扩展。启发式使用到剩余目标包围盒的 L1 下界和收缩 Die/层图的剩余代价。HBT 可能跨越不同对齐网格的格中心，因此单位位移价格取所有边允许的下界，层图只保留扣除该位移后的剩余 HBT 价格。整体多终端连接及按单节点存最短回溯链属于候选生成。即使逐步放宽到全密度，也不能据一次失败断言任意三维树均不存在。

`routing_statistics.json` 的 stages 含实际候选数量、完成网数、运行时间、有限池的 best_feasible、lower_bound、gap、solver_status。candidate_generation=TIME_LIMIT 说明对应模式域仍有未枚举部分；即使有限池 gap 为 0，扩大候选空间后仍可能改善。全物理空间 gap 独立留空。运行没有全网可行解时，程序以非零状态退出，整网质量指标留空。

## 复杂度及当前边界

设每 Die 有 L 层，S 个合法站点，每条直线最长 U 个格。单侧 L 型的金属层选择至多 O(L²)，单次跨 Die 分支需组合 S 个站点及两侧路径，受控双跨分支的站点对可达 O(S²)。若子树分别有 A、B 个资源标签，K 条连接模式，合并最多尝试 A×B×K 个边集并集。完整标签数量最坏可随资源数指数增长。当前使用有序集合存放单位边，增加可审计性，也使中间标签复制成为真实大例的瓶颈。

有限候选主问题最坏需搜索各网候选数的乘积。主问题使用显式栈；资源账本按本次变动的键增量更新。收缩层图建立下界约为 O((L₀+L₁)³)。一次稀疏 A* 至多访问当前窗口内的 V 个真实状态和 E 条邻接，队列代价 O((V+E) log V)，多终端需重复搜索。静态连通检查近似 O(E α(V))。模式搜索和多端点迷宫均另有物理合法性、资源并集及验证成本。

全局 Via 是合法层对及 GCell 级资源。track_intersections 是全局容量代理；精确 cut 几何、track 坐标和最终 Via 型号由详细布线器实现。实际 HBT 工艺的连通与间距通过给定站点表检查。每 Die 的 TritonRoute DRC 加上 HBT 两端几何及连通分量核验，可以检验当前实验协议；三维工艺签核还需要实际封装规则。

## 输出、复算和详细布线

| 文件 | 用法 |
|---|---|
| top.guide、bottom.guide | 以局部连通分量为网名，配套对应的派生 DEF |
| route_edges.tsv | 逐网、逐类型的单位边及两端真实状态，供统计复算 |
| components.jsonl | 每个局部连通分量的原始实例 Pin、普通 PORT 与 HBT landing |
| hbt_assignment.tsv | 全局 net ID，物理 site ID，bonded XY，两侧局部 XY、层、分量、固定状态 |
| routing_statistics.json | 输入库存、阶段证书与质量指标 |

`native_detail.py` 从原始 DEF 在新目录生成 placed.def。确认过的切分端口名称在派生 DEF 中复用到新 landing；额外 HBT 获取新端口，普通端口的位置和实例保持原样。写出前逐项核对原始终端的分区。支持的输入为该 benchmark 使用的 placed DEF 子集。预布线或未支持的网络约束会报错。详细布线采用外部 OpenROAD/TritonRoute。通过 `--geometry-tool` 可调用本机已有的、独立的 OpenDB 几何审计，核对导线、Via、端点分量与跨网短接，再按物理 HBT ID 合并两侧局部分量。下游工具在每颗 Die 上执行 DRC。

```bash
python3 scripts/native_bench.py /tmp/native-cugr-check/route results-3/pareto-f2f-pareto-v1 /tmp/native-seven-demo --jobs 2 --seed-seconds 1200 --stage-seconds 15
python3 scripts/native_detail.py /home/wanghh/h3d/OpenROAD/bazel-bin/openroad tests/native/clusters/manifest.json /tmp/native-clusters-demo /tmp/native-clusters-detail --geometry-tool /home/wanghh/h3d/OpenROAD/h3d_drt_validation/run_002 --parallel-dies
python3 scripts/native_detail_batch.py /home/wanghh/h3d/OpenROAD/bazel-bin/openroad /tmp/native-seven-demo /tmp/native-seven-detail /home/wanghh/h3d/OpenROAD/h3d_drt_validation/run_002 --jobs 3
python3 scripts/native_sweep.py /tmp/native-cugr-check/route /tmp/native-seven-demo/aes/manifest.json /tmp/native-aes-weights --jobs 2
```

## 分阶段验证记录

- M0 以原 revision 独立编译，保存七例各侧 14 份原始 Guide、日志、指标及哈希。
- M1 在 AES 上交换两侧输入，交叉验证独立 DEF 的 Pin/Net 数。错误的切分 PORT 与重复映射均测试拒绝。
- M2 覆盖镜像、异网格、同格不同 site、不同层对共享 site、禁区、同站点争用、非法 Via、重复 Commit/Release 和逆操作。
- M3 双网竞争两站点，精确目标和下界均为 8。两网只留同一站点时返回不可行。四端点双簇网的单/双 HBT 权衡另有端到端测试。
- M4 在 Top 横向通道加入 DEF BLOCKAGES，只允许下游金属合法借道。同 Die 模式集合不可行，跨 Die detour 找到两次键合的路径。原树与新树按同一资源状态比较。
- M5 单独启用 Maze，检查同一阻断用例的真实双 HBT 回溯；固定 Die、镜像网格及外部站点占用另有 C++ 测试。
- M6 回读两份 Guide、边表、站点表与分量，核对每个原始终端。四端点用例已用 TritonRoute 运行双侧 DRC，并用独立几何审计通过两侧连通及联合检查。七例实测数据按 global、detail 两种表分别保存。
