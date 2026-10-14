# 变量、单位、索引与文件资源参考

学习顺序见 [CODE_GUIDE.md](CODE_GUIDE.md)，完整语法与算法例子见 [CODE_REFERENCE.md](CODE_REFERENCE.md)。

本页按**作用域**解释变量。源码函数内的变量表是就地阅读入口；这里集中说明跨文件关系和容易混淆的量。

## 1. 先判定变量属于哪一层

| 层次 | 例子 | 如何理解寿命/更新 |
| --- | --- | --- |
| 对象成员 | `GridGraph::graphEdges` | 随对象保存，多次方法调用共享 |
| 函数参数 | `getWireCost(layerIndex,lower,demand)` | 调用者给本次查询输入 |
| 局部变量 | `CostT cost` | 此次调用的临时累计分 |
| 循环变量 | `for(int x=l;x<h;x++)` | 当前走到的边下标 |
| lambda 捕获 | `[&]` | 借用外层变量，未创建一份独立资料副本 |
| 代理底层地址 | `data` | 指向数据库真实记录，不是几何坐标 |
| 模板参数 | `PointT<T>` 的 `T` | 编译时选择类型，不是运行时数值 |
| 宏参数 | 枚举生成宏的 `Enum` | 编译前文本展开占位 |

名字相同不意味着变量相同。例如 `MazeRoute::getSteinerTree` 前面的 `temp` 是 `Solution` 状态游标，后面的 `temp` 是 `SteinerTreeNode` 拓扑游标。

## 2. 单位与编号的总规则

| 表示 | 含义 | 例子 |
| --- | --- | --- |
| DBU 长度 | 物理整数长度/坐标 | `position.x`、`pitch` |
| DBU² 面积 | 两个长度相乘 | `minArea`，即使字段类型也写 DBU |
| 格子索引 | 在粗网格中的位置 | `GRPoint.x/y` |
| 真实层索引 | 布线层列表下标 | 结果树的 `layerIdx` |
| 方向编号 | H=0/V=1 | `GridGraphView` 第一维 |
| 对象编号 | 对象集合身份或下标 | `Net.index` |
| 局部成员编号 | 所属模板/候选列表内位置 | `pinIndex`、`pathIndex` |
| 规则档位 | 查宽度/长度间距表的槽 | `w/l` |
| 计数 | 已有多少项 | `numPins`、`degree` |
| 字节/存储容量 | 预留多少空间 | 库头的 `*Allocated_`、`nameSize_` |
| 评分 | 为比较路线构造的小数 | `CostT`、`costs[layer]` |

例如 `libDBU=2000` 表示每微米 2000 DBU。`1000 DBU` 为 0.5 微米，既不是 1000 个格子，也不是 1000 条导线。

## 3. 入口与参数表：`main.cpp/global.h`

| 变量 | 保存什么 | 数值/更新方式 |
| --- | --- | --- |
| `argc` | 命令行项目总数 | 包括程序名，整数计数 |
| `argv[i]` | 第 i 项命令行文字 | 选项和选项值都先是文字 |
| `i` | 当前扫描项 | 从 1 开始，选项取值时先 `++i` |
| `parameters` | 解析后的运行设置 | 构造时填字段 |
| `design` | 设计资料对象 | 构造即读取 LEF/DEF |
| `globalRouter` | 路由总调度器 | 拥有资源图与网表 |
| `lef_file/def_file/out_file` | 库/设计/guide 路径 | 字符串，不是文件正文 |
| `threads` | 请求线程数 | 参数存在不证明所有循环已并行 |
| `weight_wire_length` | 线长基础权重 | 经 M2 轨距归一化 |
| `weight_via_number` | 过孔基础权重 | 一相邻层过孔基础分 |
| `weight_short_area` | 短路面积权重 | 经轨距平方与线宽换算 |
| `min_routing_layer` | 最低普通走线层索引 | 默认 1，低层引脚靠接入方案 |
| `cost_logistic_slope` | 三维资源惩罚斜率 | 影响余额改变时惩罚变化快慢 |
| `maze_logistic_slope` | 聚合二维惩罚斜率 | 服务迷宫搜索，不是时间尺度 |
| `max_detour_ratio` | 支线增长预算比例 | 默认 0.25，乘主干索引范围 |
| `target_detour_count` | 绕行采样目标 | 默认 20，不保证恰有 20 种候选 |
| `via_multiplier` | 过孔附加线需求倍率 | 默认 2，估计最小面积资源需求 |
| `pin_patch_threshold` | 引脚附近余额阈值 | 不足时考虑扩格 |
| `pin_patch_padding` | 引脚区域扩格数 | 单位格，不是 DBU |
| `wire_patch_threshold` | 导线补丁余额阈值 | 局部检查起点 |
| `wire_patch_inflation_rate` | 连续未补成功的阈值倍率 | 并非线宽扩张倍率 |
| `write_heatmap` | 是否输出余额表 | 默认 false，源码常量 |

## 4. 设计对象：`src/obj/`

### 4.1 `Design` 成员

| 成员 | 解释 | 消费者 |
| --- | --- | --- |
| `libDBU` | 每微米 DBU 换算比例 | 层/模板转换、资源图 |
| `dieRegion` | 裸片 DBU 边界 | 网格生成 |
| `layers[layer]` | 某真实层工艺资料 | 容量初始化和评分 |
| `macros[macroIndex]` | 型号+朝向的局部模板 | 实例引脚/障碍查询 |
| `instances[instanceIndex]` | 实际单元/端口 | 引脚引用定位 |
| `nets[netIndex]` | 普通信号端点名单 | `GRNet` 构造 |
| `obstacles[i]` | 设计级固定金属矩形 | 容量扣减 |
| `gridlines[0/1][i]` | x/y 第 i 条 DBU 边界 | 索引映射、guide 写出 |
| `defaultGridlineSpacing` | 缺少输入 GCell 时的边界间隔 | 3000 DBU，非 3000 微米 |
| `unit_length_wire_cost` | 每 DBU 普通线长分 | 导线评分 |
| `unit_via_cost` | 单个过孔基础分 | 过孔评分 |
| `unit_length_short_costs[layer]` | 每层每 DBU 短路面积系数 | 拥塞评分 |

### 4.2 `Macro/Instance/Net`

| 变量 | 所在类型 | 解释 |
| --- | --- | --- |
| `name` | Macro | 库型号文字名 |
| `orientation` | Macro | 旋转/镜像姿态 |
| `index` | Macro | 预留编号字段；当前构造没有赋值，不能认为传 idx 即已初始化 |
| `pinIndices[name]` | Macro | 引脚名到模板内编号的字典 |
| `pins[pin][shape]` | Macro | 本信号引脚第 shape 块局部带层矩形 |
| `obstacles[shape]` | Macro | 局部障碍，包括相关引脚金属 |
| `name/index/type` | Instance | 实际实例名字/编号/CELL 或 PORT 类别 |
| `position` | Instance | CELL 的 DBU 放置位置 |
| `macroIndex` | Instance | CELL 使用的朝向模板下标 |
| `port` | Instance | PORT 的已变换全局带层矩形 |
| `instanceIndex/pinIndex` | PinReference | 实例下标与模板内端下标，端口 pinIndex=-1 |
| `index/name/pinRefs` | Net | 普通信号网编号/名字/端点名单 |
| `layerIdx/x/y` | BoxOnLayer | 层下标及 DBU 两轴区间 |

同名 `name/index` 的范围不同：库型号名、实例名、网名不能交给同一张字典混查。

### 4.3 `MetalLayer`

| 字段 | 单位/类别 | 解释 |
| --- | --- | --- |
| `name/index` | 文字/编号 | 金属层名与相对层序号 |
| `direction` | H/V | 本层首选走线方向 |
| `width/minWidth` | DBU | 默认/最小线宽 |
| `firstTrackLoc/lastTrackLoc` | DBU | 走线轴的垂直方向首/末轨位置 |
| `pitch` | DBU | 相邻轨道间距 |
| `numTracks` | 条数 | 轨道数量，编号从 0 起 |
| `minArea` | DBU² | 最低金属面积要求 |
| `minLength` | DBU | 折算最小面积附加线长 |
| `parallelWidth[w]` | DBU | 第 w 线宽档 |
| `parallelLength[l]` | DBU | 第 l 并行长度档 |
| `parallelSpacing[w][l]` | DBU | 两条件档对应间距 |
| `defaultSpacing` | DBU | 默认宽度查询的间距摘要 |
| `maxEolSpacing/Width/Within` | DBU | 线端条目最大间隙/端宽/作用距离摘要 |

## 5. 网格线网与资源图：`src/gr/`

### 5.1 `GRNet` 和结果树

- `index/name`：从原 Net 继承的普通网编号/名字。
- `pinAccessPoints[pin][candidate]`：候选真实层/x/y 格子位置。
- `boundingBox`：所有候选的二维索引包围盒，不含层区间。
- `routingTree`：三维结果树根，尚未求解时为空。
- `GRPoint.layerIdx`：结果树为真实层；稀疏搜索临时借用为方向平面。
- `GRTreeNode.children[i]`：当前点相连的第 i 个结果孩子。

### 5.2 `GridGraph` 永久成员

| 成员 | 解释 |
| --- | --- |
| `nLayers/xSize/ySize` | 真实层数量与两轴格子数 |
| `gridlines[轴][边界]` | DBU 网格边界 |
| `gridCenters[轴][格]` | DBU 格中心 |
| `layerNames[layer]` | 输入工艺层文字名 |
| `layerDirections[layer]` | H/V 首选方向 |
| `layerMinLengths[layer]` | 估计附加线长 DBU |
| `graphEdges[layer][x][y].capacity` | 障碍扣减后的可容纳条数估计 |
| `graphEdges[layer][x][y].demand` | 已提交普通线与过孔附加需求 |
| `totalLength` | 内部已登记普通线 DBU 总长 |
| `totalNumVias` | 内部已登记相邻层过孔数 |
| `m2_pitch` | 索引 1 层轨距，归一化统计尺度 |

`GridGraphView<bool>` 第一维 H/V，值为阻塞或拥塞真假，取决于构造方法。`GridGraphView<CostT>` 同位置保存聚合分。两种 bool 表也不可随意互换：阻塞看固定容量，拥塞看容量减需求。

### 5.3 建图局部变量

| 变量 | 解释 |
| --- | --- |
| `gridTracks[layer][row]` | 某真实层某轨道排的轨道数 |
| `nGrids/nEdges` | 当前层垂直于走线的排数 / 沿走线的跨格边数 |
| `locRange/edgeInterval` | 一排物理范围 / 两格中心间物理范围 |
| `trackRange/gridTrackRange` | 查到轨道编号范围 / 当前排轨道编号范围 |
| `margin/spacing` | 扩张障碍的向量 / 距离 DBU |
| `obs/obsBox` | 原障碍矩形 / 扩大后矩形 |
| `obstaclesInGrid[row]` | 与该排相关的障碍 |
| `obstaclesAtEdge[edge]` | 当前排该边相关的障碍 |
| `usableIntervals[tIdx]` | 该排第 tIdx 轨仍可用的 DBU 区间 |
| `trackIndex/tIdx` | 全层轨道号 / 当前排内相对轨道号 |
| `minEdgeLength` | 当前层最短相邻格中心间距 DBU |

### 5.4 评分与接入选择局部变量

- `lower`：一条边的低坐标端格子位置。
- `u/v`：一段直线的两端格子位置。
- `edgeLength`：单边 DBU 长度。
- `demandLength`：需求×长度，源码用 DBU 整数保存，保留类型转换含义。
- `cost`：当前试算分。
- `loc/lowerLoc`：过孔位置与本层方向上的相邻低端。
- `lowerEdgeLength/higherEdgeLength`：低侧/高侧边长，不存在边为 0。
- `netCenter`：候选包围盒中心，格子尺度。
- `bestAccessDist.first/second`：可接入边数 / 到中心距离。
- `bestIndex`：当前最佳候选列表下标。
- `accessibility`：按固定 capacity>=1 得到的接入评分，不是当前需求余额。
- `selectedAccessPoints[hash]`：选中二维位置及必连真实层区间。

## 6. 总调度器和 guide 变量

| 变量 | 解释 |
| --- | --- |
| `netIndices` | 待办网编号，不是所有 Net 对象的副本 |
| `n1/n2/n3` | 每阶段开始时的待办数量 |
| `t1/t2/t3` | 各阶段耗时秒 |
| `halfParameters[netIndex]` | 候选包围盒宽+高，源码名字即如此 |
| `congestionView` | 第二阶段开始时的方向拥塞快照 |
| `wireCostView` | 第三阶段随提交局部刷新的聚合成本 |
| `grid` | 采样间隔/偏移设置，不是 `gridGraph` |
| `wireUsage[layer][x][y]` | 统计用普通导线次数，不含过孔附加需求 |
| `bottleneck` | 最小资源余额的位置 |
| `guides[i].first/second` | 真实层 / 闭区间格子矩形 |
| `padding` | 引脚补丁周围扩几格 |
| `l/h/r/c` | 走线轴低/高端、固定另一轴位置、当前沿线位置 |
| `patched` | 本位置是否补成功 |
| `areaOfPinPatches/areaOfWirePatches` | 未去重的格子×层补丁累计数 |
| `ss/fout` | 内存文本缓存 / 磁盘文件流 |

`getSpareResource` 中临时 `lower` 的名字表达较低位置，但当前后一分支仍将 `point.x/point.y` 传给 `getEdge`。阅读应按实际调用参数理解，而不能只由名字推断已正确访问另一侧坐标。

## 7. 模式布线变量

### 7.1 节点永久字段

- `SteinerTreeNode.children`：二维拓扑孩子；`fixedLayers`：必连真实层低/高区间。
- `PatternRoutingNode.index`：本次 DAG 唯一编号。
- `children`：原拓扑分支；`paths[child][candidate]`：每分支候选下一跳。
- `costs[layer]`：子图在该层接入的最小分，空表为尚未求解。
- `bestPaths[layer][child]`：选定候选编号及孩子层。
- `optional`：可选拐点，未选相应候选便不进入结果树。
- `numDagNodes`：已分配节点数，后置加一时同时发出新编号。
- `steinerTree/routingDag`：二维拓扑根 / 候选图根。

### 7.2 构树与评分局部字段

| 变量 | 解释 |
| --- | --- |
| `degree` | 合并后二维接入位置数，可能小于原引脚数 |
| `xs/ys` | 交给 FLUTE 的二维坐标数组，在此为格子索引 |
| `flutetree.branch[i].n` | 分支 i 的邻接分支下标 |
| `steinerPoints/adjacentList` | 分支位置表 / 无向相邻编号表 |
| `prevIndex/curIndex/nextIndex` | 上一个 / 当前 / 下一分支编号 |
| `childIndex/pathIndex` | 分支槽 / 该槽候选序号 |
| `midPoint/mid` | L 形拐点坐标 / 新建拐点对象 |
| `childCosts[child][layer]` | 当前分支该层最小后缀+连接分及候选编号 |
| `viaCosts[layer]` | 当前位置相邻层过孔的前缀和 |
| `lowLayerIndex` | 试选过孔堆叠底层 |
| `minChildCosts[child]` | 当前堆叠范围内该孩子最低分 |
| 局部 `bestPaths[child]` | 当前试选方案，维度少于最终节点字段 |
| `pathsOnLayer[layer]` | 回溯时本层要接的候选下一跳 |
| `lowestRoutingNode/highestRoutingNode` | 当前已建过孔堆叠两端 |

### 7.3 绕行局部字段

`scaffoldNodes[direction][index]` 保存暂存骨架，`scaffolds[direction]` 保存根。`visited[index]` 防共享节点重复扫描。

`trunk` 为主干走线轴范围，`stems` 为支线端点在另一轴的位置，`trunkPos` 为主干原位置，`shiftInterval` 为候选位置区间，`shiftAmount` 为相对原位置的位移。

`originalLength` 原支线总长，`maxLengthIncrease` 增长预算，`step` 采样间隔，`pos` 当前采样位置。`built/scaffolded` 是某孩子处理标记，不是全网已成功标记。

`dupShiftInterval` 当前复制后没有后续使用，不给它虚构额外算法作用。

## 8. 迷宫搜索变量

| 变量 | 解释 |
| --- | --- |
| `SparseGrid.interval/offset` | 两轴采样间隔 / 起点偏移，格子尺度 |
| `pseudoPins[pin]` | 合并后的二维位置和必连真实层区间 |
| `pxs/pys` | 端点原轴坐标列表 |
| `xs[xi]/ys[yi]` | 采样下标到原格子坐标，`xs[1]` 可为 7 |
| `vertices[v]` | 顶点 v 对应的方向平面/位置 |
| `edges[v][0/1/2]` | 前/后/切方向邻居编号，-1 无边 |
| `costs[v][slot]` | 相同槽的边成本 |
| `u/v` | 稀疏图两端顶点编号 |
| `U/V` | 这两个端点的原图二维坐标 |
| `xtoxi/ytoyi` | 原坐标到采样列表下标的字典 |
| `pinVertex[pin]` | 伪引脚到顶点编号 |
| `vertexPin[vertex]` | 顶点到伪引脚编号，非引脚顶点查得 -1 |
| `minCosts[vertex]` | 搜索已知最低累计分 |
| `queue` | 按累计分优先弹出的状态队列 |
| `visited[pin]` | 此伪引脚是否已接入当前树 |
| `numDetached` | 尚未接入伪引脚数 |
| `Solution.cost/vertex/prev` | 累计分 / 顶点编号 / 上一步状态 |
| `solutions[i]` | 第 i 次接入新伪引脚的路径终点状态 |
| `foundSolution/foundPinIndex` | 本轮终点状态 / 伪引脚编号 |
| `created[vertex]` | 已建拓扑节点，供不同回溯路径共享 |
| `lastNode` | 回溯中已经建出的下一子方向节点 |

`reset()` 修改间隔后调用 `step()`，偏移会推进，不归零。`getSteinerTree()` 的局部 `visited` 当前只创建而未用于后续去重；实际共享节点由 `created` 管。

## 9. 依赖库字段怎样查

Rsyn 的三层文件配套使用：`data` 存资料，`decl` 定菜单，`impl` 定读取/修改实现。`data` 指针来自 `Proxy`，同名指针在不同类指向不同数据类型。

- `cls` 是 Rsyn 成员命名习惯，不是单位。
- `clsMapPhysical*` 是名字索引字典，`clsPhysical*` 通常是对象集合或属性表。
- `clsHas*` 说明可选项有效，默认数值不证明输入已指定。
- `clsIndex/clsRelativeIndex` 全表/同类编号不同。
- `clsWidth[底/顶][低/高]` 是规则区间，`clsEnclosure[底/顶][x/y]` 是几何量，`clsLayers[底/切/顶]` 是三部位关联。
- `CellData/PortData` 当前为空；`LibraryData.designData` 只指所属设计，库集合在 `DesignData`。

LEF/DEF 头文件常用 `num*` 表已用数量、`*Allocated_` 表预留容量、`has*` 表是否提供。`nameSize_` 是文字空间，不是几何尺寸。

FLUTE 的 `d/deg` 为端点度数，`acc` 精度，`Branch.x/y/n` 为二维点和邻分支下标，`Tree.length` 为二维线长。`nn[i][方向槽]` 为八方向近邻，不是八层金属。

JSON 的 `m_type` 决定 `m_value` 共用存储哪个成员有效。哈希库的 `bucket/hash` 属于表内定位；性能图脚本的 `Function.id/calls` 属于函数调用图，均不是芯片对象或格子。

## 10. 所有非源码文件与运行资源

这些文件没有可解释的 C++ 局部变量。按资源种类理解它们，格式未知时不往二进制或查表记录中插入文字。

| 文件/目录 | 内容与作用 |
| --- | --- |
| `.gitignore` | 忽略 build、run*、缓存和编辑器配置的规则 |
| `README.md` | 项目介绍、构建依赖、论文和项目许可 |
| `rsyn/README.md`、`rsyn/LICENSE`、`rsyn/.gitrepo` | 依赖说明、许可、来源元数据 |
| `flute/Readme`、`ChangeLog.txt`、`license.txt` | FLUTE 使用说明、历史与许可 |
| `flute/POWV9.dat`、`POST9.dat` | 小度数 Steiner 线长候选/拓扑预计算表，运行时加载 |
| `rsyn/lib/linux/liblef.a`、`libdef.a` | 预编译解析库，声明在 include/，链接提供实现 |
| `drcu` | 详细布线二进制，将 guide 进一步细化为路由 DEF |
| `eval/ispd18eval/README`、`eval/ispd19eval/README` | 官方评分工具使用说明 |
| `eval/ispd18eval/ispd18eval.w`、`eval/ispd19eval/ispd19eval.w` | 线长、过孔、越界/规则违例等评分权重表，数值由评估程序解释 |
| `eval/*/*eval_bin` | 官方评分二进制，接收详细结果与检查报告 |
| `build/CMakeCache.txt` | 配置搜索结果和编译选项，不是路线缓存 |
| `build/Makefile`、`cmake_install.cmake`、`CMakeFiles/` | CMake 自动生成构建规则和目标文件 |
| `build/route` | 当前构建出的可执行程序 |
| `run/route`、`run/drcu`、`run/*.dat` | 便于在执行目录运行的程序/数据副本 |
| `run/*.py`、`run/eval/` | 当前运行脚本副本，源版本为 scripts/ 与 eval/ |
| `run/ispd18_sample.log` | 已有样例日志，记录运行输出，不是算法实现 |
| `run/ispd18_sample.solution.guide` | 已有样例输出区域，用于观察格式 |

日志、guide、权重表、查找表和静态库各有既定格式；它们的解释放在文档中，源码注释放在读取/写出它们的方法旁。
