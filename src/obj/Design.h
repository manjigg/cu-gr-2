// 文件功能：定义路由器使用的设计数据库，保存工艺层、单元、引脚连接、障碍和 GCell 网格。
// 构造时读取 LEF/DEF 并初始化代价；向 GridGraph 和 GRNet 提供只读设计信息。
//
// 【白话职责】这里定义“芯片资料箱”里有什么，而不是寻找导线的走法。
// LEF 给出工艺和单元模板，DEF 给出模板实例放在哪里、哪些引脚要连起来。
// 资料箱把外部 Rsyn 对象转成 CUGR 较简单的数据，减少算法直接处理文件格式的负担。
// 数据分三组：几何/工艺（layers、macros、instances、obstacles）；连接（nets）；
// 路由依据（gridlines、单位线长/过孔/短路代价）。具体读取步骤在 Design.cpp。
// class 是一种自定义类型，像设计图；Design design(params) 才建立实际资料箱。
// public 是外部可使用的接口；private 是只允许类内部管理的字段与辅助方法。
#pragma once
#include "global.h"
#include "GeoTypes.h"
#include "Layers.h"
#include "Net.h"
#include "Instance.h"

// 为 double 取一个表达用途的名字：看到 CostT 就知道这里存的是路线评分。
using CostT = double;

// 定义设计数据里保存了什么
// 这段代码说明：创建一个 Design 对象时，会自动做两件事。
// 第一件是调用 read()，读取 LEF（Library Exchange Format，库交换格式）和 DEF（Design Exchange Format，设计交换格式）文件。
// 第二件是调用 setUnitCosts()，建立后续布线过程中使用的基础代价参数。

// 这就意味着，执行完 Design design(parameters) 后，布线所需的基本设计信息已经被加载进内存。

class Design {
public:
    Design(const Parameters& params): parameters(params) {
        // const Parameters&：借用外部参数且不通过此引用修改，避免复制整份参数。
        // 冒号后的 parameters(params) 是成员初始化；花括号中才是构造函数体。
        // 参数对象应比 Design 活得更久；main 中先创建参数、后创建设计满足这个顺序。
        read(parameters.lef_file, parameters.def_file);
        setUnitCosts();
    }
    // get* 是查询函数。末尾 const 表示它不会修改本对象的数据。
    // return 把字段交给调用方；这种很短的方法直接写在 .h 中。
    DBU getLibDBU() const { return libDBU; }

    CostT getUnitLengthWireCost() const { return unit_length_wire_cost; }
    CostT getUnitViaCost() const { return unit_via_cost; }
    CostT getUnitLengthShortCost(const int layerIndex) const { return unit_length_short_costs[layerIndex]; }
    
    int getNumLayers() const { return layers.size(); }
    const MetalLayer& getLayer(int layerIndex) const { return layers[layerIndex]; }
    // pinRef 输入实例/引脚编号；pinShapes 输出追加全局 DBU 矩形，不在这里清空已有结果。
    void getPinShapes(const PinReference& pinRef, vector<BoxOnLayer>& pinShapes) const;
    
    // For global routing 
    const vector<vector<DBU>>& getGridlines() const { return gridlines; }
    // 返回 const 引用：把已有列表交给调用者查看，既不复制所有线网，也不允许修改。
    const vector<Net>& getAllNets() const { return nets; }
    void getAllObstacles(vector<vector<utils::BoxT<DBU>>>& allObstacles, bool skipM1 = true) const;
    
private:

    // libDBU	数据库长度单位	程序用多大的整数表示实际物理长度
    // dieRegion	裸片边界	整块芯片的范围
    // layers	所有参与布线的金属层	有哪些层、每层宽度和方向等
    // macros	单元模板及对应朝向的几何信息	某种单元长什么样、引脚在哪里
    // instances	实际放置的单元或端口	具体是哪一个单元、放在哪里
    // nets	需要连接的线网	哪些引脚必须相互连接
    // obstacles	布线障碍物	已被占用的几何区域
    // parameters：main 创建的参数表，只读借用，内含路径与评分/补丁设置。
    const Parameters& parameters;
    
    // libDBU：每微米对应多少库 DBU，是换算比例而不是芯片长度。
    DBU libDBU;
    // dieRegion：整个裸片的 DBU 左下/右上边界。
    utils::BoxT<DBU> dieRegion;
    // vector<T> 是长度可变化的列表；layers[i] 是编号 i 的金属层。
    vector<MetalLayer> layers;
    // macros[i] 是型号+朝向模板，不是第 i 个实际放置单元。
    vector<Macro> macros; // macros with a SPECIFIC orientation
    // instances[i] 是实际单元/端口，保存模板编号与放置位置。
    vector<Instance> instances;
    // nets[i] 是普通信号网，其连接名单尚不是求出的路线。
    vector<Net> nets;
    // obstacles 是特殊布线等设计级固定矩形；模板障碍另存于 macros。
    vector<BoxOnLayer> obstacles;
    
    // For detailed routing
    // unit_length_wire_cost：一 DBU 普通线长的基础分。
    CostT unit_length_wire_cost;
    // unit_via_cost：一相邻层过孔的基础分，不含附加线需求成本。
    CostT unit_via_cost;
    // unit_length_short_costs[layer]：该层每 DBU 线长的短路面积评分系数。
    vector<CostT> unit_length_short_costs;
    
    // For global routing
    // static 表示这项常量属于类型，不必每个对象单独存一份。
    // 输入没有 GCell 定义时使用，单位为 DBU，不能直接理解为 3000 微米。
    const static int defaultGridlineSpacing = 3000;
    // 两层 vector 是“列表里的列表”：gridlines[0] 是 x 边界，gridlines[1] 是 y 边界。
    vector<vector<DBU>> gridlines;
    
    void read(std::string lef_file, std::string def_file);
    void setUnitCosts();
};
