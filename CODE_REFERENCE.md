# CUGR 2.0 语法、算法推演与逐文件参考

**学习入口：[CODE_GUIDE.md](CODE_GUIDE.md)。变量查询：[VARIABLE_REFERENCE.md](VARIABLE_REFERENCE.md)。**

本页保留完整语法讲解、逐文件目录和手算例子，用于查阅。第一次阅读先沿学习入口的执行顺序前进，遇到术语或算法细节再来本页找对应章节。

这份文档按“没有 C++ 基础，但希望真正看懂项目”的读者编写。你可以先把代码当作处理资料的一系列动作，不需要先背完 C++ 语法。

**怎么使用这份文档：**

- 第 0 节先解释芯片布线和源码里常见的 C++ 写法。
- 第 1～4 节建立项目地图，找到每个文件负责什么。
- 第 5～10 节解释算法关系与依赖模块。
- 第 13～20 节用小例子从参数读取一路推演到 guide 输出，并逐步拆解动态规划和迷宫搜索。
- 读源码时，先读文件顶部“白话职责/零基础阅读”，再读函数旁的变量与步骤说明。

文中的数字例子用于教学；如果没有明确写成当前源码默认值，就不是实际 benchmark 的测量结果。带代码框的简化示例说明语言或算法概念，真正行为仍以源码为准。

## 0. 从零开始：先把名词和代码符号看懂

### 0.1 把芯片看成一座多层城市

想象有一座城市：

- 建筑物对应已经摆好的**单元实例**。
- 建筑物上的门对应**引脚**，导线需要从这些位置接入。
- 一份“这些门必须互相接通”的名单对应**线网**。
- 不同高度的道路对应**金属层**。
- 楼层之间的电梯对应**过孔**，用来换到另一层。
- 不允许穿越的区域对应**障碍**。
- 把城市地图分成大格子对应**GCell 全局布线网格**。

这里“线网”不是一根已经画好的线。例如 U1 的 A 引脚、U2 的 B 引脚、U3 的 C 引脚必须属于同一个电信号，这三者组成一条线网。我们需要找一棵连接树让它们全部接通。

一条线网也不必是一条不分叉的线。多个引脚可以共享一段主干，然后分叉到不同引脚。

### 0.2 为什么芯片要分金属层

如果所有导线都只在一个平面，很多连接会挤在一起。同一层不同信号随意交叉可能造成短路；使用多层金属允许信号在不同高度通过。

这个项目的全局模型给每层一个**首选方向**：某层主要水平走，另一层主要垂直走。要从横向转到纵向，常需要通过过孔换到方向不同的层。

因此，路线不仅回答“平面上经过哪里”，还要回答“在哪个金属层经过、在哪个位置换层”。

```text
二维路线：A ---- 拐点
                  |
                  B

带层信息：A --水平层导线-- 拐点
                           |
                         过孔换层
                           |
                         垂直层导线
                           |
                           B
```

这个图只表达概念；具体引脚层和换层数量需要算法结合工艺与资源决定。

### 0.3 全局布线和详细布线有什么区别

全局布线先回答：**大致从哪些网格区域通过、用哪些层比较合适？**

详细布线再回答：**准确在哪条轨道放金属、用哪种过孔、线宽与间距是否满足规则？**

可以类比导航的两级安排：先选城区和道路走廊，再落实到具体车道与每个路口的动作。

CUGR 的输出是 `guide` 区域。它不是所有制造细节已经落实的最终金属图。`drcu` 用 guide 继续详细布线，生成路由 DEF；评估工具再检查详细布线结果。

### 0.4 LEF 和 DEF 分别提供什么

| 文件 | 白话理解 | 典型资料 |
| --- | --- | --- |
| LEF | 工艺规范和零件型号手册 | 金属层、线宽、间距、轨距相关信息、单元模板、模板引脚形状 |
| DEF | 这块具体芯片的装配和连接清单 | 裸片范围、实际单元位置/朝向、端口、线网、轨道、GCell、已有特殊布线 |

同一个型号可以在 DEF 中出现许多次。就像同一种电阻可以买一百个，型号手册只要一份，但每个实际电阻的位置和连接不同。

### 0.5 `.h`、`.cpp`、`.c`：不要把文件当成执行顺序

`.h` 通常定义类型和声明方法，告诉编译器“有这些东西”。`.cpp` 通常写 C++ 方法实现；`.c` 通常是 C 实现。某些很短的实现也直接写在 `.h` 中。

`#include "obj/Design.h"` 表示编译时把相关定义引进来，不表示运行到此处才读取这个头文件。

程序运行从 `main()` 开始，遇到函数调用时进入对应函数。一个 `.cpp` 中函数写在前面还是后面，不直接决定运行先后。

```text
源码和头文件
    ↓ 编译：检查语法并生成机器代码片段
各个目标文件 + 静态库
    ↓ 链接：把函数引用与实现对应起来
route 可执行程序
    ↓ 运行：从 main 按调用关系执行
读取输入 → 建图 → 选路 → 写结果
```

### 0.6 变量：有名字的资料盒子

```cpp
int threads = 1;
double cost = 0.5;
bool congested = false;
std::string name = "net1";
```

逐行解释：

1. `int` 存整数，`threads` 是盒子名，先装 `1`。
2. `double` 能存小数，`cost` 先装 `0.5`。
3. `bool` 存 `true/false`，表示是/否。
4. `std::string` 存一段文字，`name` 先装 `net1`。

`=` 是赋值，“把右边结果放到左边”，不是判断是否相等。判断相等用 `==`。

```cpp
cost = cost + 2;
cost += 2;
```

两句都表示在原值上加 2。`i++` 表示把 `i` 加一，但当前表达式先使用旧值；`++i` 先加一再使用新值。

### 0.7 函数：有输入、动作和结果的小步骤

```cpp
int add(int a, int b) {
    return a + b;
}
```

`int` 是返回结果类型；`add` 是函数名；括号里的 `a/b` 是输入；花括号里是工作内容；`return` 将结果交回调用方。

`void` 表示没有普通返回值，但函数仍可能修改对象、写文件或输出日志。例如 `GlobalRouter::route()` 返回类型是 `void`，计算结果存在对象字段里。

因此不能只问“函数 return 了什么”，还要问“它改了哪些数据”。

### 0.8 类、对象、构造函数：型号图纸与实际资料箱

```cpp
Design design(parameters);
```

这句话的三部分分别是：

- `Design`：类型，规定这个资料箱有哪些字段和方法。
- `design`：这个实际资料箱的名字。
- `parameters`：交给构造过程的参数。

**创建对象会调用构造函数。**本项目的 `Design` 构造函数会调用 `read()` 和 `setUnitCosts()`。所以这句话执行完，设计已经被读取，并不是只声明一个空名字。

`class` 的 `public` 是外部可用菜单，`private` 是内部资料和辅助方法。它们帮助把对象管理规则集中在一个地方。

### 0.9 `.`、`->`、`::` 分别怎么读

| 写法 | 白话读法 |
| --- | --- |
| `design.getAllNets()` | 调用这个设计对象的线网查询方法 |
| `node->children` | 根据节点地址找到对象，再取它的孩子列表 |
| `GlobalRouter::route` | `GlobalRouter` 这个类型的 `route` 方法 |
| `std::vector` | `std` 名字空间里的 `vector` 类型 |

`namespace` 像名字的分类目录，防止不同库都叫同一个函数名而冲突。

### 0.10 引用、指针与 `const`：复制资料，还是借用资料

看这三种概念：

```cpp
void inspect(Design design);        // 形式上按值传入
void inspect(const Design& design); // 只读借用
void change(Design& design);        // 可修改借用
```

它们是语言示意，不是在本仓库增加的方法。

`&` 用在参数/变量类型后表示引用，可以先理解为给原对象起另一个使用名字。`const &` 表示通过这个引用只读查看，减少大对象复制。

本项目有些类型是代理，本身很小；即使按值复制代理，也可能仍共享同一底层资料。因此“按值”是否复制完整设计，还需看具体类型。

`T*` 是指针，保存 T 对象的地址。`nullptr` 表示不指向有效对象。指针的地址数字不是坐标、层号或对象编号。

`std::shared_ptr<T>` 会共享管理 T 对象的寿命。多个候选路径可以指向同一个 DAG 节点；最后一个拥有者释放后自动清理它。共享对象意味着修改也可能被其他共享者看见，不能把每个指针都想成独立复制品。

### 0.11 容器：列表、字典和集合

| 写法 | 白话理解 | 项目例子 |
| --- | --- | --- |
| `vector<T>` | 长度可变的 T 列表 | 所有金属层、所有线网 |
| `vector<vector<T>>` | 列表里的列表 | 每个引脚的候选接入点 |
| `unordered_map<K,V>` | 根据钥匙 K 快速找值 V | 实例名字 → 实例编号 |
| `unordered_set<K>` | 只记哪些钥匙出现过 | 网格接入点去重 |
| `pair<A,B>` | 把两项装在一起 | 坐标与必需层区间 |
| `array<T,3>` | 固定三个位置的表 | 稀疏图顶点的三个邻居槽 |

列表从索引 0 开始。若 `layers.size()==3`，合法下标是 0、1、2，不包含 3。

- `size()`：目前装了几项。
- `reserve(n)`：预留存储空间，列表项数不变。
- `resize(n)`：把项数调整到 n，必要时创建新项。
- `push_back(v)`：往末尾加入已有值。
- `emplace_back(args...)`：用这些参数在末尾直接构造一项。
- `back()`：访问最后一项。
- `clear()`：清空本列表，不自动清空别的对象列表。
- `find(key)`：找某把钥匙，等于 `end()` 表示没找到。

例如清空 `netIndices` 只是清空待处理编号；并不会把 `nets` 中已生成的路线树全部删掉。

### 0.12 多维数组怎样看

```cpp
graphEdges[layer][x][y]
pinAccessPoints[pinIndex][candidateIndex]
```

第一句依次问：“哪层 → 哪个 x → 哪个 y”，得到一条资源边。第二句依次问：“哪个引脚 → 它的哪个候选”。

两层/三层 `vector` 是索引组织层级，不意味着芯片只含两层/三层金属。

### 0.13 条件与循环：只需要先看执行规则

```cpp
if (resource < 0) {
    // 有溢出时执行这里
}
for (int i = 0; i < count; i++) {
    // 对索引 0 到 count-1 逐个执行
}
for (const auto& net : nets) {
    // 逐个只读查看线网
}
```

- `&&`：两个条件都满足。
- `||`：至少一个满足。
- `!`：把是/否反过来。
- `continue`：跳过本轮剩余动作，继续下一轮。
- `break`：结束当前循环。
- `while(condition)`：条件为真就继续重复。
- `auto`：让编译器推断类型，不表示随便什么类型都能混用。

### 0.14 三目表达式和类型转换

```cpp
reverse ? -demand : demand
```

读成：“如果 reverse 为真用负需求，否则用正需求”。它是 `if/else` 的短写形式，本项目用来登记或撤回占用。

`static_cast<DBU>(value)` 明确做类型转换。小数转整数可能丢小数部分；若要四舍五入，源码会先调用 `round`。

这与数学公式的精确小数计算可能不同。例如本项目单边成本中的 `demandLength` 用整数 DBU 保存，逐数值复现时要保留这一步转换。

### 0.15 lambda、递归、模板先这样理解

**lambda 是就地写的一小段函数：**

```cpp
[&](int lhs, int rhs) {
    return scores[lhs] < scores[rhs];
}
```

它接两个编号，比较分数；`[&]` 表示借用外面已有变量。`sort` 用这种函数决定谁排前面；树遍历用这种函数决定每个节点要做什么。

**递归是函数调用自己来处理更小部分：**看当前树节点，然后对每个孩子做同样工作。没有孩子时停止向下，返回上一层。

**模板是一份可适用不同类型的代码配方：**`PointT<int>` 放整数格子坐标，`PointT<DBU>` 放物理整数坐标。算法结构相似，但使用方赋予的单位不同。

### 0.16 怎么读一个陌生函数：固定问五个问题

1. 谁调用它？这决定它在完整流程中的位置。
2. 输入是什么？文件名、对象、索引、坐标还是候选树？
3. 输出在哪里？返回值、成员字段、引用参数还是磁盘文件？
4. 哪些数据会变？例如修改 `net.routingTree` 与修改 `graphEdges.demand` 是两件事。
5. 前提是什么？例如需要先建图、先有有效根节点、先读 FLUTE 表。

不要先试图把每个符号都背下来。先搞清这五项，再看每个循环的输入如何变成结果。

## 1. 先理解项目在做什么

这是一个面向芯片物理设计的**全局布线器**。输入是描述工艺与单元库的 LEF 和描述具体设计的 DEF；输出是每个信号线网在不同金属层上的布线引导区域（route guide）。

可以把它理解成：先把芯片划成网格，估算每条网格边能容纳多少导线，再为所有需要互连的引脚选择连接拓扑、路径及金属层，尽量减少拥塞并改善后续详细布线的可行性。

本仓库对应 README 中的 EDGE / DAG-based global routing engine。主算法集中在 `src/gr/`，理解项目时应先沿主流程读，再深入依赖库。

### 推荐阅读路线

1. [`src/main.cpp`](src/main.cpp)：看清程序生命周期。
2. [`src/global.h`](src/global.h)：理解输入参数和算法常量。
3. [`src/obj/Design.h`](src/obj/Design.h)、[`Design.cpp`](src/obj/Design.cpp)：理解输入如何转换成路由数据。
4. [`src/gr/GRNet.h`](src/gr/GRNet.h)、[`GRTree.h`](src/gr/GRTree.h)：理解线网和结果树。
5. [`src/gr/GlobalRouter.cpp`](src/gr/GlobalRouter.cpp)：掌握三个阶段以及结果输出。
6. [`src/gr/GridGraph.cpp`](src/gr/GridGraph.cpp)：理解容量、需求和拥塞代价。
7. [`src/gr/PatternRoute.cpp`](src/gr/PatternRoute.cpp)：阅读候选 DAG、动态规划和层选择。
8. [`src/gr/MazeRoute.cpp`](src/gr/MazeRoute.cpp)：阅读拥塞线网的稀疏图搜索。
9. 最后按需要查阅 Rsyn 导入代码、FLUTE 及评估脚本。

## 2. 目录地图与注释范围

| 目录/文件 | 职责 | 阅读重点 |
| --- | --- | --- |
| `src/main.cpp`、`src/global.h` | 程序入口与全局配置 | 从输入到输出的连接 |
| `src/obj/` | CUGR 的精简设计数据模型 | 工艺层、实例、引脚、障碍、线网 |
| `src/gr/` | 全局布线算法与共享资源图 | 拓扑、候选路径、拥塞、金属层 |
| `src/utils/` | 几何、日志、打印、枚举与哈希工具 | 被算法复用的基础设施 |
| `rsyn/src/rsyn/` | 逻辑/物理设计数据库及读取服务 | LEF/DEF 到设计对象的转换 |
| `rsyn/include/` | LEF/DEF 5.8 库接口头文件 | 底层解析回调的对象类型 |
| `rsyn/lib/linux/` | 预编译 LEF/DEF 静态库 | CMake 链接输入 |
| `flute/` | FLUTE Steiner 树算法及辅助模块 | 初始二维连接拓扑 |
| `scripts/` | 构建、批量运行、分析与画图 | 怎么运行并观察结果 |
| `eval/` | ISPD 2018/2019 评估脚本与工具 | 详细布线结果的评分 |
| `drcu` | Dr. CU 详细布线器二进制 | 将 guide 细化成实际布线 DEF |
| `build/` | CMake 自动生成文件和编译产物 | 构建检查使用 |
| `run/` | 可执行文件、查找表和运行脚本副本 | 执行环境与已有结果 |

本次为 **347 个版本管理中的源码/构建脚本**添加文件顶部中文职责说明，其中 `src` 32 个、`rsyn` 288 个、`flute` 18 个、`scripts` 5 个、`eval` 4 个。另为当前 `run/` 中 **9 个 Python/Shell/Tcl 副本**添加说明。C/C++ 的无扩展名导出头文件也包含在范围内。

核心实现的注释同时解释常见 C++ 语法、字段含义、输入输出和步骤原因，并加入坐标、容量、候选路径等例子。依赖库各文件增加零基础阅读说明，解释该对象像什么、数据与接口怎样配合、相关数字属于哪种单位/类别以及如何跳转到下一层实现。原版权与已有注释保留。

`*.dat` 是 FLUTE 预计算数据，`*.w` 是评估权重表，`*.a` 与可执行文件是二进制；`build/` 内的 CMake 文件与编译器检测代码由构建系统生成。它们不作为手写源码注释。`run/` 被 `.gitignore` 忽略，重新复制运行文件时会由 `scripts/` 和 `eval/` 的源版本更新。

## 3. 一次布线的完整执行链

```text
main(argc, argv)
  ├─ Parameters：解析 -lef / -def / -output / -threads
  ├─ Design
  │    ├─ read：通过 Rsyn 读取 LEF/DEF
  │    │    ├─ 工艺金属层与轨道
  │    │    ├─ 指定朝向的 Macro、放置实例与端口
  │    │    ├─ 信号线网与引脚引用
  │    │    ├─ 特殊线网固定障碍
  │    │    └─ GCell 网格边界
  │    └─ setUnitCosts：建立线长、过孔、短路面积代价
  ├─ GlobalRouter 构造
  │    ├─ GridGraph：容量初始化与障碍扣减
  │    └─ GRNet：物理引脚形状映射成候选网格接入点
  ├─ GlobalRouter::route
  │    ├─ 阶段 1：FLUTE + 候选 DAG + 模式布线
  │    ├─ 阶段 2：拥塞线网拆线 + 加入绕行候选 + 模式布线
  │    └─ 阶段 3：稀疏图迷宫搜索 + 新拓扑 + 模式布线层选择
  └─ GlobalRouter::write
       ├─ 从三维布线树生成基础 guide
       ├─ 添加引脚接入补丁与导线补丁
       └─ 网格索引转 DBU 矩形并写入输出文件
```

`GlobalRouter` 持有共享的 `GridGraph` 和全体 `GRNet`。单线网路由器读取资源图并修改对应线网的树；总调度器负责提交/撤回资源需求。这样后续线网能够看到已布线网占用的资源。

## 4. 核心代码逐文件说明

### 4.1 入口和构建

| 文件 | 实现内容 | 重点符号 |
| --- | --- | --- |
| [`src/main.cpp`](src/main.cpp) | 初始化参数、导入设计、执行布线、写出 guide、打印结束资源信息 | `main` |
| [`src/global.h`](src/global.h) | 统一依赖、Boost 空间索引类型与参数解析；保存算法固定参数 | `Parameters`、`RTree` |
| [`src/CMakeLists.txt`](src/CMakeLists.txt) | 汇总 CUGR/Rsyn/FLUTE 源文件，设置 C++14、头文件路径并链接依赖 | `add_executable(route ...)` |

### 4.2 设计对象：`src/obj/`

| 文件 | 实现内容 | 重点符号 |
| --- | --- | --- |
| [`Design.h`](src/obj/Design.h) | 设计数据的所有权和只读查询接口；构造时读入设计并计算代价 | `Design` |
| [`Design.cpp`](src/obj/Design.cpp) | 导入层、朝向模板、实例、线网、特殊线网障碍与 GCell；换算引脚坐标和汇总障碍 | `read`、`setUnitCosts`、`getPinShapes`、`getAllObstacles` |
| [`Instance.h`](src/obj/Instance.h) | 定义单元模板和放置实例；区分单元与外部端口 | `Macro`、`Instance` |
| [`Instance.cpp`](src/obj/Instance.cpp) | 提取并旋转/镜像引脚和障碍形状，建立引脚名索引，计算端口矩形 | `Macro::Macro`、`Instance::Instance` |
| [`Layers.h`](src/obj/Layers.h) | 定义层宽、方向、轨道及间距/最小面积相关规则 | `MetalLayer` |
| [`Layers.cpp`](src/obj/Layers.cpp) | 读取层规则，换算 DBU，查询物理范围内的轨道和并行间距 | `rangeSearchTracks`、`getParallelSpacing` |
| [`Net.h`](src/obj/Net.h) | 表达线网及其要连接的实例/引脚引用 | `Net`、`PinReference` |
| [`Net.cpp`](src/obj/Net.cpp) | 当前只有头文件包含，方法在 `Net.h` 中内联 | 编译单元入口 |
| [`GeoTypes.h`](src/obj/GeoTypes.h) | 带层索引的 DBU 矩形及 Rsyn 边界转换声明 | `BoxOnLayer` |
| [`GeoTypes.cpp`](src/obj/GeoTypes.cpp) | 边界转换、矩形打印和同层/相邻层几何连通判断 | `getBoxFromRsynBounds`、`isConnected` |

**模板与实例的区别：**`Macro` 保存指定朝向的局部形状，`Instance` 保存放置位置和模板索引。查询单元引脚时才加实例位置。模板缓存键包含朝向，所以同名库单元在不同朝向下可有不同模板。

**线网引用的区别：**单元引脚由 `(instanceIndex, pinIndex)` 定位；外部端口用 `pinIndex=-1` 标识。电源/地线网不作为普通信号网布线，特殊线网已有金属几何参与障碍统计。

### 4.3 路由核心：`src/gr/`

| 文件 | 实现内容 | 重点符号 |
| --- | --- | --- |
| [`GRNet.h`](src/gr/GRNet.h) | 网格级线网：候选接入点、包围盒和结果树 | `GRNet` |
| [`GRNet.cpp`](src/gr/GRNet.cpp) | 将物理引脚矩形映射到相交 GCell，按三维位置去重 | `GRNet::GRNet` |
| [`GRTree.h`](src/gr/GRTree.h) | 三维网格点及父子连接的结果树 | `GRPoint`、`GRTreeNode` |
| [`GRTree.cpp`](src/gr/GRTree.cpp) | 前序遍历及调试打印，服务资源提交和 guide 生成 | `preorder`、`print` |
| [`GridGraph.h`](src/gr/GridGraph.h) | 容量/需求边、三维资源图和二维方向视图接口 | `GraphEdge`、`GridGraph`、`GridGraphView` |
| [`GridGraph.cpp`](src/gr/GridGraph.cpp) | 容量估计、障碍扣减、几何查询、代价、需求提交/撤回、溢出检查和热力图 | `getWireCost`、`getViaCost`、`commitTree`、`extractWireCostView` |
| [`GlobalRouter.h`](src/gr/GlobalRouter.h) | 三阶段调度、线网/资源网格持有、输出和统计声明 | `GlobalRouter` |
| [`GlobalRouter.cpp`](src/gr/GlobalRouter.cpp) | 按包围盒半周长排序、三阶段路由、拥塞筛选、guide 补丁和统计 | `route`、`getGuides`、`write` |
| [`PatternRoute.h`](src/gr/PatternRoute.h) | 二维拓扑节点、候选 DAG 节点、动态规划状态和模式布线器 | `SteinerTreeNode`、`PatternRoutingNode`、`PatternRoute` |
| [`PatternRoute.cpp`](src/gr/PatternRoute.cpp) | FLUTE 拓扑、直线/L 形候选、拥塞主干绕行、动态规划及结果回溯 | `constructSteinerTree`、`constructDetours`、`calculateRoutingCosts`、`getRoutingTree` |
| [`MazeRoute.h`](src/gr/MazeRoute.h) | 稀疏采样参数、方向双平面图和最短路状态 | `SparseGrid`、`SparseGraph`、`Solution`、`MazeRoute` |
| [`MazeRoute.cpp`](src/gr/MazeRoute.cpp) | 强制加入引脚坐标的稀疏图、优先队列搜索、路径回溯和拓扑简化 | `SparseGraph::init`、`MazeRoute::run`、`getSteinerTree` |

### 4.4 基础工具：`src/utils/`

| 文件 | 实现内容 |
| --- | --- |
| [`utils.h`](src/utils/utils.h) | 汇总几何、日志、打印和枚举头文件 |
| [`geo.h`](src/utils/geo.h) | 点/区间/矩形/线段模板；曼哈顿等距离；矩形合并和切片 |
| [`log.h`](src/utils/log.h) | 计时器、内存查询和日志接口声明 |
| [`log.cpp`](src/utils/log.cpp) | 带时间戳日志与 Linux/Windows 内存统计 |
| [`prettyprint.h`](src/utils/prettyprint.h) | 通用容器流式打印与分隔符策略 |
| [`enum.h`](src/utils/enum.h) | Better Enums：生成可遍历及整数/字符串转换的枚举 |
| [`robin_hood.h`](src/utils/robin_hood.h) | Robin Hood 哈希表：供接入点去重、模板缓存和顶点索引使用 |

## 5. 三个路由阶段如何协作

### 阶段 1：先快速得到完整解

处理全部线网，按网格包围盒半周长升序排序。每个线网依次执行：

1. `selectAccessPoints` 从每个引脚的候选 GCell 中选位置。
2. `constructSteinerTree` 调用 FLUTE 生成二维连接拓扑。
3. `constructRoutingDAG` 将每条拓扑边展开为直线或两种 L 形候选。
4. `run` 求不同路径与金属层的最低代价组合。
5. `commitTree` 把需求提交到共享资源图。

完成后遍历结果树，筛选有导线溢出的线网。

### 阶段 2：在模式布线中加入绕行

阶段开始时提取二维拥塞快照。逐个撤回拥塞线网旧需求，重新建立拓扑/DAG，用 `constructDetours` 添加可平移主干的候选，再求最低代价路线并提交。

`ScaffoldNode` 用来组织有拥塞的同方向片段；`trunk` 是主干范围，`stems` 是连接外部端点的支线位置。`max_detour_ratio` 限制支线总长度增量相对主干长度的比例，`target_detour_count` 控制绕行采样密度。

### 阶段 3：更灵活地改变连接拓扑

先整体撤回仍拥塞的线网，再聚合同方向各金属层的容量/需求形成二维代价视图。

`SparseGraph` 使用间隔采样线并强制纳入引脚坐标；每个二维位置在 H/V 两个方向平面中各有一个顶点。顶点最多连接同方向前后两个位置和另一个方向平面。

`MazeRoute::run` 使用 Dijkstra 式优先队列扩展。每连到一个未连接伪引脚，就把找到的路径变成零代价源，让下一条路径能够接到已经形成的树上。回溯之后合并共享路径、重合点和共线中间点。

**搜索结果仍是二维拓扑。**`GlobalRouter` 把它送回 `PatternRoute` 的 DAG 优化流程，完成真实金属层选择，再提交新树并增量刷新二维代价。

## 6. 读算法必须分清的几个概念

### 6.1 坐标和编号

| 表示 | 含义 | 常见位置 |
| --- | --- | --- |
| `DBU`、`BoxOnLayer` | 物理整数坐标；单位由输入设计指定 | 库几何、实例位置、输出矩形 |
| `GRPoint(layerIdx,x,y)` | 金属层索引与 GCell 索引 | 实际路由树与接入点 |
| `MetalLayer::H=0`、`V=1` | x/y 方向 | 层首选方向、二维视图 |
| `SparseGraph` 内 `GRPoint.layerIdx` | 暂时借用为 H/V 平面编号 | 迷宫搜索顶点 |
| `gridlines[d][i]` | 第 i 条物理网格边界 | 索引到 DBU 的转换 |

层索引从 0 开始，默认 `min_routing_layer=1`，通常对应 M2。长度区间 `range()` 是 `high-low`；如果统计闭区间包含的整数网格数量，需要 `+1`。

### 6.2 容量、需求与代价

```text
resource = capacity - demand
resource < 0  =>  资源溢出
```

容量先按轨道数初始化，再扣除障碍影响。障碍会按间距规则扩张；每条轨道的有效长度按比例计入容量，所以容量可以是小数。

单条网格导线边的代价近似为：

```text
需求线长 × 单位线长代价
  + 需求线长 × 该层单位短路长度代价 × 拥塞惩罚
```

拥塞惩罚使用 `1/(1+exp(resource*slope))`；容量不足一轨时直接使用较大惩罚。过孔包含固定成本，以及上下金属层满足最小面积所需的估计额外导线成本。

`commitTree(tree, true)` 撤回旧需求，`commitTree(tree)` 提交新需求。判断线网是否需要重布时遍历导线边；资源余额还包含过孔附近的估计需求。输出的溢出统计与详细布线 DRC 是不同层次的指标。

### 6.3 二维拓扑、DAG 与三维结果树

| 对象 | 要回答的问题 |
| --- | --- |
| `SteinerTreeNode` | 哪些二维位置需要连接？ |
| `PatternRoutingNode` | 同一连接有哪些直线/L 形/绕行候选？ |
| `GRTreeNode` | 最终选了哪条路径、在哪些真实金属层上连接？ |

DAG 节点的 `costs[layer]` 缓存从该层接入的最低子图代价；`bestPaths[layer][child]` 保存最佳候选路径与层。动态规划自底向上求解子路径，再枚举覆盖 `fixedLayers` 的过孔层区间。回溯时按 `bestPaths` 建立三维节点，并补齐必需的引脚层连接。

## 7. guide 输出与后续评估

`GlobalRouter::getGuides` 包含三部分：

1. **基础区域：**同层线段转换成网格矩形，过孔堆叠在经过的每层添加网格位置。
2. **引脚接入补丁：**低于最小布线层的接入点扩展到上方布线层，必要时添加周围网格 padding。
3. **导线补丁：**资源紧张的位置在有余量的相邻层补充区域。

写出格式为：

```text
net_name
(
lx ly hx hy layer_name
...
)
```

矩形坐标是 DBU，网格高端使用 `gridline(high+1)`，覆盖最后一个网格的完整物理范围。guide 表示推荐布线区域。

评估链为：`route -> guide -> drcu -> routed DEF -> Innovus 检查 -> ispdXXeval_bin 评分`。

## 8. Rsyn 文件如何定位

### 8.1 输入导入与服务

以下路径均相对 `rsyn/src/rsyn/`。

| 文件/文件对 | 职责 |
| --- | --- |
| `session/Session.h/.cpp` | 共享设计状态、Reader/Service 注册与文件查找 |
| `session/Reader.h` | 输入读取基类，衔接网表填充 |
| `session/Service.h` | 服务启动/停止的接口约定 |
| `setup/reader.cpp`、`setup/service.cpp` | 注册内置读取器和服务 |
| `io/reader/ISPD2018Reader.h/.cpp` | 当前 LEF/DEF 读取入口；解析后填充网表并启动物理服务 |
| `io/reader/PopulateRsyn.h/.cpp` | 中间描述转逻辑库、实例、端口与线网连接 |
| `io/parser/lef_def/LEFControlParser.h/.cpp` | LEF 库回调转 `LefDscp` |
| `io/parser/lef_def/DEFControlParser.h/.cpp` | DEF 库回调转 `DefDscp` |
| `io/parser/guide-ispd18/GuideDescriptor.h` | guide 中间数据结构 |
| `io/parser/guide-ispd18/GuideParser.h/.cpp` | guide 文本读取 |
| `io/parser/parser_helper.h/.cpp` | 旧式输入格式的解析类型和分词/记录处理 |
| `io/legacy/Legacy.h` | LEF/Bookshelf 的方向、用途字符串转换 |
| `io/legacy/PlacerInternals.h` | 兼容布局导入的内部描述类型 |
| `ispd18/Guide.h`、`ispd18/RoutingGuide.h/.cpp` | 分层 guide 表示与按线网导入的服务 |
| `phy/PhysicalService.h/.cpp` | 持有物理设计并响应逻辑网表变化 |
| `phy/PhysicalDesign.h` | 物理对象聚合入口 |
| `phy/obj/impl/PhysicalDesign.cpp` | 加载物理库/设计，建立工艺与布局对象 |
| `phy/PhysicalRouting.h/.cpp` | 实际导线、过孔、矩形及几何转换 |
| `phy/util/DefDescriptors.h`、`LefDescriptors.h` | DEF/LEF 的中间描述结构 |
| `phy/util/PhysicalTransform.h/.cpp` | 点/矩形的旋转、镜像与朝向变换 |
| `phy/util/PhysicalTypes.h` | 物理类型、方向和过孔层级枚举 |
| `phy/util/PhysicalUtil.h`、`PhysicalLayerUtil.cpp` | 枚举/字符串及物理名称转换 |

### 8.2 逻辑对象的三层组织：`core/`

`core/Rsyn.h` 按依赖顺序聚合网表对象；`core/RsynTypes.h` 定义公共枚举。`core/dscp/LibraryCell.h` 与 `LibraryModule.h` 是库对象的导入描述。

要按当前实际结构区别占位与存储：`CellData`、`PortData` 当前没有成员；`LibraryData` 仅持有所属 `DesignData*`；库集合和名字索引位于 `DesignData`。不能仅从文件名推断每个同名 data 文件都保存一整套独立资料。

对于下面每个对象名，仓库通常分别提供：

```text
core/obj/data/<对象>.h   底层字段与关联
core/obj/decl/<对象>.h   公开代理接口
core/obj/impl/<对象>.h   内联访问/修改方法
```

| 对象名 | 语义 |
| --- | --- |
| `Object` | 公共对象身份与基础数据 |
| `Design` | 全设计、对象创建和连接管理 |
| `Module` | 模块中的实例/线网/端口集合 |
| `Instance` | 单元、模块或端口的实例公共信息 |
| `Cell` | 库单元实例 |
| `Port` | 模块边界端口 |
| `Net` | 线网及其驱动/负载连接 |
| `Pin` | 实例引脚与线网、库引脚关联 |
| `Arc` | 引脚间逻辑弧 |
| `Library` | 逻辑库集合 |
| `LibraryCell` | 可复用库单元模板 |
| `LibraryModule` | 库模块表示 |
| `LibraryPin` | 库引脚的名称、方向和用途 |
| `LibraryArc` | 库单元内部的弧模板 |

`core/infra/Attribute.h` 提供对象附加属性；`List.h` 管理集合；`RangeBasedLoop.h` 适配遍历；`Observer.h` 定义变更回调；`RawPointer.h` 处理内部指针转换；`Exception.h` 提供核心异常。

### 8.3 物理对象的三层组织：`phy/obj/`

规律同样是 `data` 存储、`decl` 接口、`impl` 内联实现；多数数据文件以 `Data.h` 结尾。每个文件的顶部注释指出它处于哪一层。

| 对象族 | 作用 |
| --- | --- |
| `PhysicalDesign` | 整体物理对象集合及逻辑/物理映射 |
| `PhysicalDie` | 裸片边界 |
| `PhysicalGCell` | 全局布线网格线重复定义 |
| `PhysicalTracks` | 布线轨道定义 |
| `PhysicalLayer` | 工艺层和规则 |
| `PhysicalInstance`、`PhysicalCell`、`PhysicalModule`、`PhysicalPort` | 实例、单元、模块和端口的物理属性 |
| `PhysicalLibraryCell`、`PhysicalLibraryPin` | 库单元/引脚几何 |
| `PhysicalPin`、`PhysicalPinGeometry`、`PhysicalPinLayer` | 实例引脚、几何组、单层形状 |
| `PhysicalObstacle` | 单元障碍 |
| `PhysicalNet` | 线网几何边界与路径 |
| `PhysicalSpecialNet`、`PhysicalSpecialWire` | 特殊线网及导线 |
| `PhysicalRoutingGrid`、`PhysicalRoutingPoint` | 布线网格与路径点 |
| `PhysicalRow`、`PhysicalSite` | 放置行与 site |
| `PhysicalRegion`、`PhysicalGroup` | 区域约束与分组 |
| `PhysicalSpacing` | 间距条目 |
| `PhysicalVia`、`PhysicalViaGeometry` | 过孔与过孔形状 |
| `PhysicalViaRule`、`PhysicalViaRuleBase`、`PhysicalViaRuleGenerate` | 过孔公共/固定/生成规则 |
| `LayerViaManager` | 按工艺层组织过孔 |

额外的数据文件：`PhysicalObject.h` 是公共物理对象基础；`PhysicalSpacingRuleData.h` 与 `PhysicalSpacingTableData.h` 保存间距规则/表；`ViaGeometryData.h` 和 `ViaRuleData.h` 保存过孔几何/规则。`phy/infra/PhysicalAttribute.h` 挂接物理属性，`PhysicalObserver.h` 定义物理变更通知。

### 8.4 工具、序列化与导出头

| 文件/文件族（相对 `rsyn/src/rsyn/`） | 作用 |
| --- | --- |
| `util/Proxy.h` | 共享底层数据的轻量代理，基于对象 id 排序/哈希 |
| `util/Array.h`、`RangeBasedLoop.h` | 多维数组与迭代适配 |
| `util/dbu.h`、`dim.h`、`double2.h`、`float2.h` | DBU/维度及二维坐标 |
| `util/Bounds.h`、`DoubleRectangle.h`、`FloatRectangle.h` | 不同坐标类型的矩形 |
| `util/geometry/Point.h`、`Polygon.h/.cpp`、`Rect.h/.cpp` | 几何点、多边形和矩形 |
| `util/Units.h`、`FloatingPoint.h` | 单位与浮点比较 |
| `util/Environment.h`、`String.h`、`TristateFlag.h` | 环境变量、字符串、三态配置 |
| `util/Stopwatch.h`、`MemoryUsage.h`、`AsciiProgressBar.h` | 时间、内存与进度 |
| `util/ThreadPool.h` | 任务队列和线程同步 |
| `util/Color.h`、`Colorize.h`、`Stipple.h` | 可视化颜色与图案 |
| `util/Debug.h`、`Exception.h` | 断言及异常 |
| `util/MD5.h` | 消息摘要 |
| `util/Json.h`、`3rdparty/json/json.hpp` | JSON 别名和完整 JSON 库 |
| `db/Database.h/.cpp` | 数据库读写接口；`.cpp` 当前是占位文件 |
| `db/Serializable.h`、`SerializationStream.h` | 序列化对象与流的接口声明 |
| `export/Rsyn/*` | 仅转发 include 的无扩展名入口 |

导出入口有 `DesignObserver`、`PhysicalDesign`、`PhysicalDesignObserver`、`Point`、`Polygon`、`Rect`、`RoutingGuide`、`Session`、`Scenario` 和 `Timer`。其中 `Scenario`/`Timer` 指向本精简仓库未提供的模块，当前路由入口不使用它们。

### 8.5 LEF/DEF 库头文件：`rsyn/include/`

这些文件主要声明底层库对象及接口；具体实现来自 `rsyn/lib/linux/liblef.a` 和 `libdef.a`。

| 路径/文件族 | 职责 |
| --- | --- |
| `lef5.8/lefiLayer.hpp`、`lefiMacro.hpp` | 工艺层规则、单元、引脚和障碍 |
| `lef5.8/lefiVia.hpp`、`lefiViaRule.hpp` | 固定过孔与过孔规则 |
| `lef5.8/lefiArray.hpp`、`lefiMisc.hpp`、`lefiUnits.hpp` | 阵列、通用几何/site、物理单位 |
| `lef5.8/lefiCrossTalk.hpp`、`lefiNonDefault.hpp` | 串扰模型与非默认规则 |
| `lef5.8/lefiProp.hpp`、`lefiPropType.hpp` | 自定义属性与类型 |
| `lef5.8/lefiDebug.hpp`、`lefiDefs.hpp`、`lefiKRDefs.hpp`、`lefiUtil.hpp`、`lefiEncryptInt.hpp`、`lefiUser.hpp` | 调试、公共类型、兼容宏、助手、加密输入、对象聚合 |
| `lef5.8/lefrReader.hpp`、`lefrCallBacks.hpp`、`lefrSettings.hpp`、`lefrData.hpp` | LEF 读取、回调、设置与解析状态 |
| `lef5.8/lefwWriter.hpp`、`lefwWriterCalls.hpp`、`lefzlib.hpp` | LEF 写出、写回调与压缩读取 |
| `def5.8/defiComponent.hpp`、`defiPinCap.hpp`、`defiNet.hpp`、`defiPath.hpp` | 实例、端口/电容、线网与路径 |
| `def5.8/defiRowTrack.hpp`、`defiSite.hpp` | 放置行、轨道、GCell 和 site |
| `def5.8/defiVia.hpp`、`defiBlockage.hpp`、`defiRegion.hpp`、`defiGroup.hpp` | 过孔、阻塞、区域和分组 |
| `def5.8/defiFill.hpp`、`defiSlot.hpp`、`defiMisc.hpp` | 金属填充、开槽、通用几何/样式 |
| `def5.8/defiAssertion.hpp`、`defiFPC.hpp`、`defiIOTiming.hpp`、`defiTimingDisable.hpp`、`defiPartition.hpp`、`defiScanchain.hpp` | 断言、布局约束、时序、分区与扫描链 |
| `def5.8/defiNonDefault.hpp`、`defiPinProp.hpp`、`defiProp.hpp`、`defiPropType.hpp` | 非默认规则、引脚属性与属性定义 |
| `def5.8/defiAlias.hpp`、`defiDebug.hpp`、`defiDefs.hpp`、`defiKRDefs.hpp`、`defiUtil.hpp`、`defiUser.hpp` | 别名、调试、类型、兼容宏、助手与聚合入口 |
| `def5.8/defrReader.hpp`、`defrCallBacks.hpp`、`defrSettings.hpp`、`defrData.hpp` | DEF 读取、回调、设置与解析状态 |
| `def5.8/defwWriter.hpp`、`defwWriterCalls.hpp`、`defzlib.hpp` | DEF 写出、写回调与压缩读取 |

## 9. FLUTE 文件说明

| 文件（相对 `flute/`） | 职责 |
| --- | --- |
| `flute.h` | 精度常量、`Branch`/`Tree` 和构树/线长接口 |
| `flute.c` | 读取预计算表、低/中阶构树、分解合并和线长估计 |
| `flute_mst.c` | 高阶点集的 MST 辅助构树和局部优化 |
| `mst2.h/.c` | 二维 MST 的接口、初始化和构建 |
| `neighbors.h/.c` | 八方向近邻的暴力/分治计算 |
| `heap.h/.c` | Prim 算法使用的可降键二叉堆 |
| `dist.h/.c` | 整数点的曼哈顿距离 |
| `dl.h/.c` | 双向链表管理与排序 |
| `memAlloc.h/.c` | 带索引上下界的向量/矩阵/三维数组分配与释放 |
| `err.h/.c` | 错误报告与退出 |
| `global.h` | MST/近邻辅助模块公共点类型和常量 |
| `POWV9.dat`、`POST9.dat` | 线长候选与 Steiner 拓扑预计算表 |

`PatternRoute::readFluteLUT` 在阶段 1 前加载查找表；运行时表文件需要位于当前工作目录。FLUTE 给出连接拓扑后，CUGR 会按拥塞和真实层代价重新选择路线。

## 10. 脚本逐文件说明与运行方式

| 文件 | 职责 |
| --- | --- |
| [`scripts/build.py`](scripts/build.py) | 构建模式到 CMake 参数的映射；编译并准备运行目录 |
| [`scripts/run_base.py`](scripts/run_base.py) | Bash 执行、调试模式前缀和算例全名/别名/集合 |
| [`scripts/run.py`](scripts/run.py) | 按算例执行 route/eval/view，保存日志、guide、DEF 和图 |
| [`scripts/gprof2dot.py`](scripts/gprof2dot.py) | 将性能分析数据转换成 DOT 调用图 |
| [`scripts/visualize_heatmap.py`](scripts/visualize_heatmap.py) | 将各层资源余额画成 PDF |
| `eval/ispd18eval/ispd18eval.sh` | 准备 Innovus Tcl，运行 ISPD 2018 检查并评分 |
| `eval/ispd18eval/ispd18eval.tcl` | 几何/连通检查设置与报告输出 |
| `eval/ispd19eval/ispd19eval.sh` | 裁剪或合并 DEF，运行 ISPD 2019 检查并评分 |
| `eval/ispd19eval/ispd19eval.tcl` | 加载设计和增量路由，执行 DRC/连通检查 |

从项目根目录构建：

```bash
python3 scripts/build.py -o release
```

在 `run/` 目录执行（将示例路径替换为实际文件）：

```bash
./route -lef /path/design.input.lef -def /path/design.input.def -threads 8 -output design.solution.guide
```

批量脚本示例，同样在 `run/` 目录执行：

```bash
python3 run.py 9t1 -p /path/to/benchmark/root -s route -t 8
```

脚本当前使用 `<benchmark_root>/iccad2019c/<算例全名>/<算例全名>.input.lef/.def` 组织输入，未指定 `-p` 时读取 `BENCHMARK_PATH`。

当前 C++ 参数解析器只处理 `-lef`、`-def`、`-output` 和 `-threads`；`run.py` 中保留的 `rrrIters` 等历史选项未被当前解析器实现。主路由循环按线网顺序执行，不能仅凭存在 `-threads` 参数就判断这些循环已经并行。

`write_heatmap` 默认是 `false`，是源码常量；若启用，`GridGraph::write` 输出 `heatmap.txt`，`run.py` 会调用画图函数。独立绘图文件当前提供函数定义，命令行参数解析入口未实现。

## 11. 按问题找代码

| 想理解或调整的问题 | 先看哪里 |
| --- | --- |
| 输入设计保存了什么？ | `Design.h` 与 `Design::read` |
| 为什么引脚位置受朝向影响？ | `Instance.cpp` 与 `PhysicalTransform.cpp` |
| 怎样从物理矩形得到 GCell？ | `GRNet.cpp`、`rangeSearchCells` |
| 为什么某条边容量不足？ | `GridGraph` 构造函数中的轨道计数/障碍扣减 |
| 拥塞如何影响候选选择？ | `GridGraph::getWireCost`、`PatternRoute::calculateRoutingCosts` |
| 为什么要加过孔？ | `fixedLayers`、`getViaCost`、`getRoutingTree` |
| 绕行候选怎样生成？ | `constructDetours` 的 scaffold/trunk/stems |
| 迷宫搜索为什么更灵活？ | `SparseGraph::init` 与 `MazeRoute::run` |
| 输出 guide 为什么比路径大？ | `GlobalRouter::getGuides` 的两类补丁 |
| 最终质量如何评估？ | `scripts/run.py::evaluate` 与 `eval/` |

## 12. 本次修改的验证

- 检查 347 个版本管理源码/脚本与 9 个运行副本，均在文件开头提供功能说明。
- 对 C/C++、CMake、Shell、Tcl 比较去除整行注释/空白后的内容；对 Python 比较 AST，确认可执行逻辑保持一致。
- Shell 脚本通过 `sh -n` 语法检查。
- Tcl 文本通过 `info complete` 结构完整性检查；检查没有执行 Innovus 的专用命令。
- 使用 `cmake --build build --parallel 2` 成功编译并链接 `route`。
- 差异检查按原文件 CRLF 行尾设置执行：`git -c core.whitespace=cr-at-eol diff --check`。

上述检查覆盖注释、脚本结构与编译；本次未重新执行完整 benchmark 的路由和商业工具评估。

## 13. 第一课：沿 `main.cpp` 逐句看程序实际做了什么

### 13.1 输入是怎样交给程序的

假设命令是：

```bash
./route -lef a.lef -def a.def -output a.guide -threads 8
```

操作系统把它分成 9 项：

| 下标 | `argv` 中的文字 | 作用 |
| --- | --- | --- |
| 0 | `./route` | 程序名字 |
| 1 | `-lef` | 下一项是 LEF 路径 |
| 2 | `a.lef` | LEF 路径 |
| 3 | `-def` | 下一项是 DEF 路径 |
| 4 | `a.def` | DEF 路径 |
| 5 | `-output` | 下一项是输出路径 |
| 6 | `a.guide` | 输出 guide 路径 |
| 7 | `-threads` | 下一项是线程数文字 |
| 8 | `8` | 需要转成整数的文字 |

`argc==9`。`Parameters` 的循环从 `i=1` 开始，因为 `argv[0]` 不是选项。

遇到 `-lef` 时，`strcmp(argv[i], "-lef")==0` 为真。`argv[++i]` 先把 i 从 1 增到 2，再取 `a.lef`，保存到 `lef_file`。本轮结束 `for` 的 `i++` 再把 i 增到 3，于是下一轮正好看 `-def`。

为什么两次增一不是错误？因为一组选项占两项：一项名字，一项值。

代码假定需要值的选项确实给了下一项，不在这里新增参数校验。理解源实现时应知道这种前提。

### 13.2 创建对象之后，内存里多了什么

| 执行到哪里 | 已有的数据 | 还未完成的工作 |
| --- | --- | --- |
| `Parameters parameters(...)` | 文件路径、运行字段、默认算法常量 | 尚未读芯片 |
| `Design design(parameters)` | 层、模板、实例、线网、障碍、网格线、单位成本 | 尚未选择路线 |
| `GlobalRouter globalRouter(...)` | 容量图、网格线网候选接入点 | 尚未求三阶段结果 |
| `globalRouter.route()` | 每条线网当前结果树及已登记需求 | 尚未输出 guide |
| `globalRouter.write()` | 磁盘上的 guide | 可交后续详细布线 |

这个表尤其适合检查自己是否误把构造函数当成“只声明一个变量”。

### 13.3 先后顺序为什么重要

不能先生成接入点再读取设计，因为接入点需要物理引脚矩形与网格边界。

不能先写最终 guide 再求路线，因为 `getGuides` 要遍历已选三维树。

不能先运行 FLUTE 再读查找表，因为低阶算法依赖预计算资料。

“函数存在”与“现在调用它所需资料已齐”是两回事。

## 14. 第二课：设计资料怎样从名字变成可用几何

### 14.1 为什么需要两层数据库

Rsyn 能认识复杂的 LEF/DEF，并提供很多通用逻辑/物理对象；CUGR 的主算法只需要其中一部分。

`Design::read` 做转换，把当前算法需要的字段整理成较简单的 `MetalLayer/Macro/Instance/Net/BoxOnLayer`。

```text
LEF/DEF 文本
  ↓ 底层解析库识别语法
lefi*/defi* 解析对象
  ↓ Rsyn ControlParser 的回调提取字段
LefDscp/DefDscp 临时描述
  ↓ PopulateRsyn 建逻辑库/实例/线网
Rsyn 逻辑网表
  ↓ PhysicalDesign 加载库几何/设计布局并建立映射
Rsyn 物理设计
  ↓ CUGR Design::read 提取当前算法需要的资料
CUGR 精简设计对象
```

每一步有不同职责。解析器先把字读懂，数据库把对象关系建好，路由器才开始计算路线。

### 14.2 模板、朝向和实例的数字例子

假设库模板有信号引脚 A，局部矩形为：

```text
左下角 (0,0)，右上角 (10,5)
```

一个同朝向实例 U1 放在 `(100,200)`，平移后形状为：

```text
左下角 (100,200)，右上角 (110,205)
```

另一个 U2 放在 `(300,400)`，使用同一个朝向模板，但平移为：

```text
左下角 (300,400)，右上角 (310,405)
```

因此模板无需存两份相同几何。两个实例各记位置即可。

如果 U2 被旋转或镜像，引脚相对零件的哪一侧也会改变，所以 CUGR 缓存的是**指定朝向的模板**。`Macro` 构造先处理朝向，`Design::getPinShapes` 再加实例位移。

数字例子只说明平移。任意旋转的实际结果要结合模板尺寸、原点和 `PhysicalTransform` 规则，不能直接把 x/y 交换就认为正确。

### 14.3 从线网名字到引脚矩形的查询链

假设信号网 N1 要接 U1.A 和 U2.B。

1. `instanceIndices` 字典把 `U1` 换成实例列表编号，例如 3。
2. U1 的 `macroIndex` 指向其型号/朝向模板。
3. 模板的 `pinIndices` 将 `A` 换成该模板引脚编号，例如 0。
4. `Net` 保存 `PinReference(3,0)`。
5. `getPinShapes` 用实例编号找到 U1，取模板引脚 0 的全部矩形并加 U1 的位置。

```text
线网 N1
  └─ PinReference(instanceIndex=3,pinIndex=0)
        ├─ instances[3]：位置/模板编号
        └─ macros[模板编号].pins[0]：局部形状集合
              ↓ 平移
           芯片坐标形状集合
```

外部端口不按普通模板引脚查询，用 `pinIndex=-1` 标记，直接取实例中保存的端口矩形。

### 14.4 为什么会把引脚金属也记成障碍

引脚需要允许所属信号接入，但其金属也占据空间，并不代表其他信号可以自由穿过。

`Macro` 提取引脚时，普通信号引脚形状加入 `pins`，所有相关引脚金属形状也加入 `obstacles`。电源/地引脚没有普通信号编号，但仍可作为占用几何存在。

全局模型采用近似容量表示，接入选择与容量扣减不是详细布线器的完整合法性处理。读代码时要分别看“给本信号哪些候选接入点”和“已有金属减少多少资源”。

## 15. 第三课：亲手推演物理坐标、网格和资源账本

### 15.1 DBU 到网格编号

假设 x 边界为 `[0,100,200,300]`，y 边界也一样。芯片被分成 `3×3` 个格子。

```text
y
300 +-----+-----+-----+
    |0,2  |1,2  |2,2  |
200 +-----+-----+-----+
    |0,1  |1,1  |2,1  |
100 +-----+-----+-----+
    |0,0  |1,0  |2,0  |
  0 +-----+-----+-----+ → x
    0    100   200   300
```

点 `(150,50)` 的物理位置落在格子 `(1,0)`。这两个数字不是同一种坐标：150/50 是 DBU，1/0 是索引。

引脚矩形若是 `(110,20)-(190,80)`，只覆盖一个格子 `(1,0)`。如果形状是 `(90,20)-(210,80)`，会与 x 索引 0、1、2 的格子相交，产生多个候选。

边界相接的细节以 `rangeSearchRows` 的实现为准。源码对正好落在边界上的高端做修正，不应照搬无限开区间/闭区间的抽象描述。

### 15.2 网格点和网格边

水平层上 `(0,1)` 到 `(1,1)` 是一条网格边，存储在低端 `(0,1)`。

从 `(0,1)` 走到 `(2,1)`，要经过两条边：低端索引 0 和 1。所以相关循环是 `x<h`，不是 `x<=h`。

垂直层的道理一样，只是变化的是 y。

格子中心坐标是相邻边界平均值。上述格子的 x 中心是 50、150、250，相邻中心距离为 100 DBU。

实际输入网格可能不等距，因此 `getEdgeLength` 读中心距离，而不是所有边都乘同一个固定宽度。

### 15.3 一条边的容量为什么可以是 1.5

假设一个通道里原来有 3 条轨道，障碍扣减后：

| 轨道 | 可用长度比例 |
| --- | --- |
| 第 0 条 | 1.0 |
| 第 1 条 | 0.5 |
| 第 2 条 | 0.0 |

全局模型累加得到 `capacity=1.5`。

这是资源估计分数，不表示制造时能放“半条完整导线”。详细布线还需落实真实轨道连续性与规则。

障碍为什么要先扩大？因为新导线不是零宽细线，还要留工艺间距。导线中心没进入原矩形，也可能因为半线宽和间距碰到障碍要求。

### 15.4 提交、撤回、重布的账本例子

以 `capacity=2` 的某条边为例，忽略额外过孔需求，只演示普通导线：

| 操作 | demand | resource=capacity-demand |
| --- | --- | --- |
| 初始 | 0 | 2 |
| 提交网 A | 1 | 1 |
| 提交网 B | 2 | 0 |
| 提交网 C | 3 | -1，溢出 |
| 撤回网 C 旧树 | 2 | 0 |
| 网 C 改走别处并提交 | 此边仍为 2 | 0 |

为什么重布前要撤回旧树？若不撤回，候选评分会看到 C 自己的旧占用，而且提交新树后可能同时把旧新需求都记进去。

阶段 2 逐网撤回再提交，阶段 3 先整体撤回全部待修复网，再建立聚合二维成本。两者对其余待修复网占用的观察时点不同。

### 15.5 拥塞惩罚的直观数值

`logistic(r,s)=1/(1+exp(r×s))`，示例取 `s=1`：

| 剩余资源 r | 惩罚近似值 | 直觉 |
| --- | --- | --- |
| 5 | 0.0067 | 资源很多，拥塞项较小 |
| 1 | 0.2689 | 有余量，但不是完全不罚 |
| 0 | 0.5 | 正好用满 |
| -1 | 0.7311 | 已经溢出，罚得更多 |

实际代码在单边 `capacity<1` 时不用这个结果，直接给拥塞因子 1；其他路径按其条件执行。

若需求线长是 100、单位线长分 0.005、单位短路长度分 0.02：

```text
资源很多：100×0.005 + 100×0.02×0.0067 ≈ 0.5134
资源为 0：100×0.005 + 100×0.02×0.5    = 1.5
```

这说明平面长度一样的线段，也可能因资源不同有不同代价。

真实代码还有 DBU 整数转换、当前参数和各层线宽，不能把这组教学数值当实际默认评分结果。

### 15.6 二维视图为什么有两层列表

假设 M2/M4 都水平，M3/M5 都垂直。二维代价视图把水平层容量/需求合成一个水平分数，把垂直层合成一个垂直分数。

`view[H][x][y]` 问“从聚合角度，在这里水平走多少钱”，不是问“真实 M1 多少钱”。

这样搜索图更小，但真实层选择不能省。聚合有余量不保证任意真实层都有余量，后面的 `PatternRoute` 需要逐层重新评分。

## 16. 第四课：把模式布线和动态规划拆成能手算的步骤

### 16.1 三种树/图分别保存什么

```text
SteinerTreeNode：A 需要连 B/C
          ↓ 把连接展开成候选
PatternRoutingNode：连 B 可以选 L1/L2/绕行，连 C 也有候选
          ↓ 计算分数并回溯
GRTreeNode：已经选 L2，在层 1 横走、层 2 竖走，并在拐点换层
```

拓扑回答连接关系，候选图回答允许的走法，结果树回答实际选择。名字都带 Node，但作用不同。

### 16.2 两点如何生成两种 L 形候选

取 `A=(1,1)`、`B=(4,3)`：

```text
候选 L1：A(1,1) → P(4,1) → B(4,3)
候选 L2：A(1,1) → Q(1,3) → B(4,3)
```

L1 先横后竖；L2 先竖后横。曼哈顿长度都是 `|4-1|+|3-1|=5`。

但 L1 的水平段可能经过拥堵边，L2 的垂直段也可能有障碍。因此“长度一样”不表示算法随便选哪条都一样。

`constructPaths` 创建 P/Q 这两个可选拐点。它们的 `optional=true`，表示某条候选用它才需要它，并不是说引脚可随便不接。

如果 A/B 同 x 或同 y，就只需要直线候选，不建无意义的 L 形拐点。

### 16.3 `children` 和 `paths` 怎样区分

假设当前节点 N 的原拓扑孩子是 B/C：

```text
children = [B,C]
paths[0] = [通向 B 的候选下一跳 P, 通向 B 的候选下一跳 Q]
paths[1] = [通向 C 的候选下一跳 R]
```

`paths[0][1]` 是“第 0 个孩子的第 1 种候选”，它指向下一跳 Q，继续沿 Q 的候选链才到 B。

所以这不是“每个候选 value 都存整个路径数组”，而是节点指针组织出的候选子图。

### 16.4 DAG 共享为什么能省工作

两条不同拐弯路线最终都接到 B，B 往下还有一棵子树。如果 B 子问题每次重新算，很多工作重复。

`node->costs` 已非空说明这个子问题已算，后续候选直接复用。可以类比做作业：同一个小题算过并写在草稿纸上，别处再用它时直接查答案。

这就是动态规划的“记忆”，不是把所有可能路线全存成完整拷贝。

### 16.5 一个孩子的评分怎样组合

为某个孩子有候选 P/Q，假设在某层上的教学分数为：

| 候选 | 后续子图分 | 当前连接线段分 | 合计 |
| --- | --- | --- | --- |
| P | 6 | 3 | 9 |
| Q | 4 | 2 | 6 |

在这个层接入该孩子时，选 Q，保存分数 6 和候选编号。

`calculateRoutingCosts` 实际对每个孩子、每个候选、每个方向匹配的真实层重复这种比较。

方向不匹配的层会跳过：水平连接不能在本模型的垂直首选层当作合法水平边计分。

### 16.6 为什么多孩子不能只各自选最便宜一层

假设 N 有两个孩子：

| 孩子 | 层 1 连接分 | 层 2 连接分 |
| --- | --- | --- |
| B | 2 | 7 |
| C | 8 | 3 |

若分别选最便宜，就让 B 从层 1 接入、C 从层 2 接入。这两段仍需在 N 位置通过过孔连起来。

忽略换层分会漏算成本。反过来，若两个孩子在同一层略贵但不需要额外过孔，也可能整体更便宜。

真实程序因此枚举 N 位置覆盖哪些层，对该范围里每个孩子选最低接入分，再加堆叠过孔成本。

### 16.7 `fixedLayers` 是什么硬性需要

N 位置若需要连索引 0 层的引脚和索引 2 层的引脚，`fixedLayers` 必须覆盖 `[0,2]`。

不能因为“只覆盖层 1 更便宜”就丢掉层 0/2 的引脚连接。代价优化是在满足连接约束的候选里选低分，不是拿最低分替代必须连通的要求。

程序还会按访问点策略向上扩展约束范围，为层/轨道切换留余地。

### 16.8 过孔前缀和手算

假设某位置相邻层过孔成本依次为 2、3、4。前缀值：

```text
viaCosts[0]=0
viaCosts[1]=2
viaCosts[2]=5
viaCosts[3]=9
```

从层 1 到层 3 的堆叠成本是 `viaCosts[3]-viaCosts[1]=9-2=7`，即相邻过孔 3+4。

前缀和让很多层区间比较无需反复从头累加。这里的成本是包含局部资源估计的过孔成本，不只是固定过孔数量权重。

### 16.9 为何保存 `bestPaths`，不能只记最低分

考试只知道“最低花费 6”，你仍不知道要走 P 还是 Q。`bestPaths` 保存候选编号和真实层，才可以恢复具体路线。

`getRoutingTree` 的根调用先选最低 `costs` 层；对子路径用 `bestPaths` 取选择；按层分组接入，并补齐上下层的过孔堆叠与引脚要求。

这种“先求分，再恢复方案”叫回溯/重建。它不是再次重新搜索一遍所有候选。

## 17. 第五课：第二阶段的绕行究竟改了什么

### 17.1 不只是给某条边多拐一次弯

`constructDetours` 将相关同方向拥塞片段组成 scaffold（骨架）。主干称 trunk，连回原端点/外部分支的连接称 stems（支线）。

```text
原方案：端点 --支线-- 拥挤主干 --支线-- 端点
                         ↓ 平移
新候选：端点 --较长支线-- 较空主干 --较长支线-- 端点
```

主干整体移动可让几条相关分支共享绕行选择。引脚端点不能随主干任意搬走，仍需接回原位置。

### 17.2 支线长度增量的手算

设支线所在坐标为 1 和 5。主干放在 3 时，总支线长度：

```text
|1-3| + |5-3| = 4
```

若移到 7：

```text
|1-7| + |5-7| = 8
```

长度增加 4。如果主干范围长 20，默认 `max_detour_ratio=0.25` 提供的增长预算是 5，那么从这个预算概念看增加 4 属于允许尺度。

这说明为什么平移不是越远越好：绕开拥塞也不能无限增长导线。

### 17.3 候选数量与实际代码边界

`target_detour_count` 控制采样步长，避免为每一条线网生成过多候选。它不是硬保证正好有 20 条不同绕行。

`constructDetours` 中边界扩展与采样采用源码里的启发式表达式。文档解释的是组织思路；精确复现某设计候选时应按 while 条件、整数转换和边界检查逐项执行，不能将启发式当成所有路径的严格全局最优枚举。

**增加候选不等于强制使用它。**之后仍由统一动态规划比较规则路径、绕行路径及其真实层成本。

## 18. 第六课：第三阶段的迷宫搜索怎样从已有树继续接点

### 18.1 为什么先稀疏采样

若芯片有数百万网格位置，在所有实际层上完整搜索会很大。第三阶段先按 H/V 聚合资源，再每隔一段选择候选道路。

但引脚不能漏掉，所以引脚 x/y 也强制加入。假设规则 x 采样是 `[2,12,22]`，引脚在 7，则实际 xs 可包括 `[2,7,12,22]`。

xs/ys 中的**下标** xi/yi 与其保存的**原网格坐标值**不同。`xs[1]==7`，不意味着原坐标 x 为 1。

### 18.2 两个方向平面及三种邻居

在位置 `(x,y)` 有水平平面顶点 H(x,y) 和垂直平面顶点 V(x,y)。

```text
H 平面：同一 y 的前后采样 x 位置相连
V 平面：同一 x 的前后采样 y 位置相连
同位置：H(x,y) ↔ V(x,y)，允许切换方向
```

顶点最多三个邻居槽：前进、后退、切换方向。边成本沿原网格全部中间边累加，因此隔 10 格的边不会只收费一步。

这里只是搜索方向转换。真实芯片是否要从 M2 换 M3、放几只过孔，要交给之后的模式布线层分配。

### 18.3 `Solution` 是一条搜索记录

```text
Solution
  cost   ：到这里的累计搜索分
  vertex ：现在在哪个顶点
  prev   ：上一步是哪条记录
```

例如 A→P→Q→B，每到一步建立一条记录，B 的 `prev` 指 Q，Q 指 P，P 指 A。找到 B 后可以逆着地址链还原路径。

### 18.4 优先队列和剪枝的小例子

假设到 Q 已知代价 5，后来另一个候选到 Q 代价 9。后者即使排在队列里，弹出时也会被剪掉，因为 `9>minCosts[Q]`。

同一个 Q 的后续道路一样，较贵前缀没必要再全部展开。这降低重复搜索。

优先队列是“每次取当前最低累计分”的工具，不是先入先出的普通排队。

### 18.5 接通一个新引脚后为何将路径置零

首先从 A 出发找到 B：

```text
A ---- P ---- B
```

此时 A/P/B 已属于同一连通树。再接 C 时可以从 P 出发，不必总回 A：

```text
A ---- P ---- B
       |
       C
```

将已接入路径作为零代价源表达“从已有树任一点开始新增连接”。零代价不是说已有物理导线没有资源需求；这里只是本次连接搜索的累计起点安排。

每找到一个尚未连接伪引脚，`numDetached` 减一，直到所有伪引脚被接入。

这种增量连接是启发式构树。不能把“每次使用最短路队列”推断成“整棵多引脚树必然全局最短”。

### 18.6 搜索结果为什么还要清理节点

方向平面转换产生两个相同二维坐标的节点，投影成二维拓扑后需要合并。

如果 A→B→C 同一直线，B 没有引脚层要求、也没有别的分支，则可以把它压掉，保留 A→C。路线几何没改变，只少存一个中间点。

碰到相同搜索顶点时使用 `created` 字典共享节点，避免不同路径把同一段树重复建成独立拷贝。

最后把拓扑给 `PatternRoute`：再次展开 DAG、求真实层/过孔选择、提交资源、更新二维代价。迷宫阶段的输出不是直接写进 guide 的最终层结果。

## 19. 第七课：一段路线怎样变成 guide 文件

### 19.1 网格范围转物理矩形的例子

仍取 x/y 边界 `[0,100,200,300,400]`。某 guide 需要覆盖 x 索引 `[1,2]`、y 索引 `[0,0]`。

```text
左下角 = (gridline_x[1], gridline_y[0]) = (100,0)
右上角 = (gridline_x[2+1], gridline_y[0+1]) = (300,100)
```

为什么 high 加一？索引 2 的格子是 x 区间 `[200,300]`，要覆盖整个格子必须到它的右侧边界 300，而不是停在其左侧 200。

可能写出的教学记录是：

```text
N1
(
100 0 300 100 M2
)
```

层名来自实际输入工艺，不是由层索引自动拼字符串；示例 `M2` 仅便于解释。

### 19.2 过孔为什么在多个层写区域

结果树若从索引 1 层连接到索引 3 层，guide 要在 1、2、3 层给同位置区域，让后续详细布线能够逐层接入。

这些 guide 区域表达允许走廊，不在此指定具体某个过孔型号和精确切孔形状。

### 19.3 两类补丁的目的

**引脚补丁：**某引脚在低层，主路由从较高的最小路由层开始，需要给详细布线提供上下接入的区域。局部资源紧张时，扩大到周围格子。

**导线补丁：**某处导线附近资源紧张，若相邻层有余量，就额外给这个位置相邻层的 guide。

补丁加入输出区域列表，不直接改原结果树，也不等于全局模型又提交一条额外导线。

### 19.4 内存文本与磁盘文件

`stringstream ss` 先保存整段输出文字。所有网的区域拼好后，`ofstream fout(guide_file)` 打开文件，`fout << ss.str()` 把文字真正写到磁盘。

这是两阶段文本组织：拼文本与写文件。看到 `ss << ...` 时，不应认为磁盘文件已经同步产生同一行。

## 20. 实战阅读训练：怎样从懂概念变成能追代码

### 20.1 用统一模板记每个文件

读完一个文件，填写下面六项：

```text
文件：
它像什么：资料箱 / 转换器 / 计算器 / 总调度员 / 工具
输入：
输出在哪里：返回值 / 对象成员 / 引用参数 / 文件
最重要的 3 个变量：
下一步该跳到哪个文件/方法：
```

以 `GRNet.cpp` 为例：

```text
它像什么：物理引脚到网格候选的转换器
输入：Net、Design、GridGraph
输出：pinAccessPoints 和 boundingBox 成员
变量：pinRefs（端点名单）、pinShapes（物理矩形）、included（去重名单）
下一步：GridGraph::selectAccessPoints 选出当前合适入口
```

以 `GlobalRouter.cpp` 为例：

```text
它像什么：总调度员
输入：设计已转换出的 nets、共用 gridGraph、parameters
输出：每网 routingTree、全图 demand、统计与 guide 文件
变量：netIndices（待办编号）、congestionView（拥塞快照）、wireCostView（二维代价）
下一步：按阶段进入 PatternRoute 或 MazeRoute
```

### 20.2 大文件不要从第一行一路硬读到末尾

`PatternRoute.cpp` 建议按这一顺序：

1. `run`：先看到“求分 → 回溯写结果”的总体两步。
2. `constructPaths`：用两点例子理解候选下一跳。
3. `constructSteinerTree/constructRoutingDAG`：理解前置结构怎样建立。
4. `calculateRoutingCosts`：按孩子/候选/层循环理解评分表。
5. `getRoutingTree`：理解怎样从评分表恢复路线。
6. `constructDetours`：最后阅读较复杂的骨架平移与候选扩展。

`GridGraph.cpp` 建议按这一顺序：

1. 先看 `GraphEdge` 认识账本两项数字。
2. 看 `getWireCost` 和 `commitWire` 分清“只读评分”和“实际登记”。
3. 看 `commitTree` 如何把长树边展开成单边/过孔。
4. 看几何查询，理解 DBU 到格子索引转换。
5. 最后读构造函数的障碍扣减与二维视图。

### 20.3 Rsyn 三层对象怎样配套读

以 `PhysicalTracks` 为例：

1. `data/PhysicalTracksData.h`：看保存首位置、轨距、数量和所属层等资料。
2. `decl/PhysicalTracks.h`：看有哪些公开查询，返回单值还是集合。
3. `impl/PhysicalTracks.h`：看每个查询从哪个底层字段取值。
4. 回到 CUGR `MetalLayer` 构造：看这些值如何变成路由层资料。
5. 回到 `GridGraph` 构造：看如何按轨道数估计容量。

这样每一份资料都能追到用途，而不是只知道“这个头文件定义一个类”。

`Proxy` 可看作访问数据的钥匙，但要记住它不是数据所有者，也不是每次查询重新解析输入。`all*` 返回范围，由调用方循环逐个访问。

### 20.4 C、Python、Shell、Tcl 读法分别注意什么

| 语言 | 先记住的规则 | 在本项目里的用途 |
| --- | --- | --- |
| C/C++ | 类型、花括号、分号、函数调用、对象/指针 | 数据模型和路线计算 |
| Python | 缩进表示归属，`def` 定义函数，`self` 指当前对象 | 构建/批量实验/画图 |
| Shell | `$变量` 取值，`case` 分支，命令输出可管道连接 | 调外部工具和组织临时文件 |
| Tcl | `proc` 定义过程，`$变量` 取值，命令由运行环境提供 | 在 Innovus 中检查布线 |

`scripts/build.py` 自己不是 C++ 编译器。它只是拼 CMake 命令让系统工具完成编译与复制。

`scripts/run.py` 自己不是详细布线器。它调 `route` 得 guide，再按需要调 `drcu` 得路由 DEF，最后调评估工具。

Tcl 里的 `verify_drc` 是 Innovus 命令。普通 `tclsh` 即使能读完整文本，也不能替代实际芯片检查工具。

### 20.5 读完后检查能否回答这些问题

1. `Design design(parameters)` 会马上读文件吗？——会，构造函数调用 `read`。
2. Net 与 GRNet 区别？——前者保存引脚引用，后者保存网格候选与结果树。
3. `reserve(100)` 后一定有 100 项吗？——没有，只预留容量。
4. `(150,50)` 与 `(1,0)` 可直接相加吗？——不可混用，前者可为 DBU，后者是格子索引。
5. 网格容量为小数是否表示制造半根线？——不是，是全局资源近似。
6. 只比较候选会修改需求吗？——成本查询只读；选完还需提交。
7. `net.setRoutingTree` 是否已更新容量需求？——没有，GlobalRouter 还要 `commitTree`。
8. `SparseGraph` 的 0/1 是 M1/M2 吗？——不是，是 H/V 方向平面。
9. 迷宫搜索得到拓扑后为什么又用模式布线？——需要选择真实金属层和过孔。
10. guide 是否完整列出每个实际切孔？——不是，它提供区域，详细布线进一步落实。

如果这些问题能说清，你已经掌握本项目主要数据流。之后遇到长类型或复杂循环，可以用本文对应小节逐步拆开，而不是要求自己一次记住全部库实现。
