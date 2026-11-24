# 原始 CUGR 2.0 基线与数据流

本地 native-3d-f2f、2d-baseline 和 origin/2d-baseline 在开发开始时均指向 33773823b543dd4b2a2f6f5a859b2e614b9dc4c2。工作区干净，origin 为 manjigg/cu-gr-2。实际 CMake 入口在 src/CMakeLists.txt。scripts/build.py 驱动 CMake 并拷贝 FLUTE LUT。编译日志、原始二进制、七组各自两侧独立执行的日志和 Guide 保存在 results-3/native-3d/M0。

## 读入到路由网

1. src/main.cpp 的 main 构造 Parameters，再构造 Design、GlobalRouter，调用 route 和 write。Design::read 通过 ISPD2018Reader::load → parsingFlow → parseLEFFile、parseDEFFile、populateDesign 进入 Rsyn。PopulateRsyn::populateRsyn 创建逻辑库、实例与网。populateDesign 启动 rsyn.physical 后调用 PhysicalDesign::loadLibrary、loadDesign。
2. rsyn/src/rsyn/session/Session.cpp 的 Session::init 创建进程静态 sessionData、逻辑设计及库。Session::startService 在已有同名服务时返回 false。PhysicalService::start 将物理设计绑定到当时的逻辑设计并注册观察者，stop 函数体为空。MetalLayer 构造函数末尾还 delete 了 PhysicalLayer::getLayer() 返回的描述对象。连续在同一默认 Session 加载两个设计存在状态和生命周期问题。
3. src/obj/Design.cpp 将 routing layer、按朝向区分的 Macro、Cell 与顶层 PORT 实例化为 CUGR 自有数据。实例 Pin 用 (instanceIndex,pinIndex)，PORT 用 (instanceIndex,-1)。Net::addPinRef 保存有序端点。供电网排除，special-net 的金属与 Via 几何、宏 obstruction 和 pin shapes 进入障碍模型。Design::getPinShapes 对宏引脚施加实例位置平移。MetalLayer 读取优选方向轨道、宽度、间距、EOL 和 min-area 衍生最小长度。GRID 来自 DEF GCELLGRID 或默认分格。setUnitCosts 按 M2 pitch 归一化。
4. GlobalRouter 构造 GridGraph 和各 GRNet。GridGraph 按优选方向对每个 GCell 行或列数轨道，扣除障碍后得到 graphEdges 的容量。GRNet 枚举 pin shape 覆盖的 GCell，按每个原始 pin 单独去重，保留 access point 列表与 bounding box。

## 三阶段调用关系

- Stage 1 按 bounding box 半周长排序。PatternRoute::constructSteinerTree 调 GridGraph::selectAccessPoints 按可达边和中心距离选 XY，将同 XY 的层区间合并并扩展高端两层。唯一 XY 直接构根。其余以整型 GCell x/y 和 ACCURACY=3 调 flute/flute.c 的 flute。Tree 返回 deg、length 和 2d−2 个 Branch(x,y,n)。用 n 建双向邻接、跳过自指、选叶端为根，DFS 折叠同 XY 零长边并挂 fixedLayers。FLUTE 没有层或 Die 字段。flute.h 关闭了内核重复 Pin 删除。
- PatternRoute::constructRoutingDAG 递归创建 PatternRoutingNode。constructPaths 对齐时建直连，斜对角时通过两个可选弯点表示 L。run 调 calculateRoutingCosts 及 getRoutingTree。每个节点缓存 costs[layer]，逐子节点与候选路径计算同方向金属层的 getWireCost 与子问题和。Via 栈按相邻层 getViaCost 构造前缀和。枚举覆盖 fixedLayers 的层区间，累积各子节点最优入层及路径。bestPaths[parentLayer][childIndex] 保存 (pathIndex,layerIndex)。根取最低成本层，回溯构建实际 GRTreeNode 与必要的 Via 栈。
- Stage 2 用 GridGraph::checkOverflow(tree) 选择导线溢出网。extractCongestionView 把各层溢出按方向 OR 到二维表。constructDetours 构造连续拥塞方向的 scaffold，沿 trunk 主干及其侧向 stem 支线计算平移范围，按 max_detour_ratio 与 target_detour_count 生成可选节点和路径。逐网 reverse commit 后重新构造 Steiner、DAG、detours，跑同一个层分配 DP，再 commit。
- Stage 3 成批撤销剩余溢出网，extractWireCostView 聚合同方向层容量与需求。MazeRoute::constructSparsifiedGraph 合并端点坐标与 SparseGrid 的周期采样轴，在笛卡尔积上各建 H/V 两个顶点。邻居槽 0、1 连同方向前后，槽 2 连同 XY 的另一方向。代价分别为区间导线代价和单位 Via 代价，伪 Pin 处方向切换免费。run 从一个端点启动优先队列，每连上一个端点，将新路径上的顶点以零距离重新入队。getSteinerTree 以回溯链恢复分支，折叠同 XY 及共线中间点，再由 PatternRoute::setSteinerTree → constructRoutingDAG → run 确定真实金属层。随后 commitTree、updateWireCostView、SparseGrid::step。稀疏搜索属于候选拓扑生成。

## 资源和 Guide

GridGraph::getWireCost 累计实际长度费用和乘长度的 short-area 估计；logistic 以 capacity−demand 为自变量。getViaCost 加单位 Via 费用及两层满足最小面积的邻边费用。commitTree 以层号是否相等解释每条树边，commitWire 逐单位边加一并累加实际 DBU 长度，commitVia 按两层最小长度向相邻导线边分摊分数需求。reverse 取反。这里无跨 Die 边、物理站点 ID、独立 Via 容量或按网所有权。printStatistics 重新遍历树统计导线、普通 Via 与导线溢出。

GlobalRouter::getGuides 为 Wire 生成同层条带，为 Via 栈逐层加格子。在低层 Pin 上生成 Pin Access Patch，资源紧时加 padding。沿导线遍历可用资源，在邻层增加 Wire Segment Patch，并更新触发阈值。write 用 gridline 将格子闭区间换成 DEF DBU 矩形，按 net 和 layer name 写出文本。不同 Die 的局部格子索引、局部网名和金属层索引都只能在各自 Die 内解释。
