// 文件功能：汇集全局依赖、空间索引类型和 Parameters 运行参数。
// Parameters 支持单 die 和 H3D 双 die 的输入/输出；const 成员是算法固定调参值。
//
// 【本文件怎样读】先看 Parameters，再按需要回来看 include 列表。
// Parameters 是把许多配置装在一起的“参数表”，不是全局布线算法本身。
// lef_file/def_file/out_file 是路径字符串；threads 是整数；weight_* 是评分权重。
// 权重类似购物时的偏好：越看重拥塞，就越不愿意选择挤满导线的路线。
// const 表示对象创建后不能给该字段重新赋值；当前这些算法值写在源码里。
// #pragma once 告诉编译器同一编译单元只展开本头文件一次，避免重复定义。
// using/namespace 别名用来缩短类型名字，不会因此执行布线。
#pragma once

#include "utils/utils.h"
// using 把 utils 命名空间里的 log 名字引入当前范围，以后可直接写 log()。
using utils::log;
using utils::logeol;
using utils::loghline;
using utils::logmem;

// STL libraries
#include <iostream>
#include <string>
#include <csignal>
#include <vector>
// #include <unordered_map>
// #include <unordered_set>
#include <thread>
#include <mutex>
#include <set>
#include <tuple>
#include <bitset>
#include <sstream>
#include <fstream>

#include "utils/robin_hood.h"

// Rsyn
#include "rsyn/session/Session.h"
#define RSYN_NO_GUI
#include "rsyn/core/Rsyn.h"
#include "rsyn/phy/PhysicalService.h"
#include "rsyn/ispd18/RoutingGuide.h"
#include "rsyn/io/reader/ISPD2018Reader.h"

// Boost libraries
#include <boost/icl/split_interval_map.hpp>
#include <boost/geometry.hpp>
#include <boost/geometry/geometries/point.hpp>
#include <boost/geometry/geometries/box.hpp>
#include <boost/geometry/index/rtree.hpp>
#include <boost/foreach.hpp>
#include <boost/functional/hash.hpp>

// 命名空间像名字的文件夹：boost::geometry 表示 Boost 的几何工具部分。
// bg/bgi 只是简写，省掉反复书写很长的命名空间名字。
namespace bg = boost::geometry;
namespace bgi = boost::geometry::index;

using boostPoint = bg::model::point<DBU, 2, bg::cs::cartesian>;
using boostBox = bg::model::box<boostPoint>;
// RTree 是空间索引类型，能按矩形范围找对象；不是路线结果中的树。
// pair<boostBox,int> 把“几何范围”和“对象编号”装成一对。
using RTree = bgi::rtree<std::pair<boostBox, int>, bgi::rstar<32>>;

// 物理长度以 DBU 表示；代价权重用于平衡线长、过孔与拥塞风险。
struct Parameters {
    // lef_file/def_file/out_file：输入库/输入设计/输出 guide 路径；threads：请求线程数。
    // weight_wire_length/via_number/short_area：线长/过孔/短路面积评分权重。
    // min_routing_layer 真实层下标；cost_logistic_slope 三维拥塞斜率；maze_logistic_slope 聚合搜索斜率。
    // max_detour_ratio 支线增长预算比例，target_detour_count 绕行采样目标，via_multiplier 面积需求估计倍数。
    // pin_patch_threshold/wire_patch_threshold 资源余额阈值；pin_patch_padding 周边格数；
    // wire_patch_inflation_rate 连续未补成功时的阈值倍率；write_heatmap 是否写资源余额表。
    // struct 把相关字段和函数放在一起，默认可从外部访问这些字段。
    // std::string 是可变长文字。此处只保存路径，并不存放 LEF/DEF 文件内容。
    std::string lef_file;
    std::string def_file;
    std::string out_file;
    std::string top_def_file;
    std::string bottom_def_file;
    std::string hbt_config_file;
    std::string output_dir;
    std::string h3d_netlist_file;
    std::string h3d_net_selection_file;
    bool h3d_init_only = false;
    bool h3d_topology_only = false;
    // paper 按公开论文算法运行；enhanced 显式启用全域精确解与未来站点预算。
    std::string h3d_method = "paper";
    std::uint32_t h3d_seed = 0;
    std::string h3d_paper_ties = "coalesce";
    int h3d_assignment_trials = 1;
    bool isH3D() const { return !top_def_file.empty() || !bottom_def_file.empty(); }
    // =1 给出默认值；之后 -threads 的文字参数会转换为整数覆盖它。
    int threads = 1;
    // 
    // double 能存小数。三项分别影响总线长、过孔数量和预计短路面积的代价。
    // “代价”是用来比较候选的分数，越小越好，不是付款金额。
    const double weight_wire_length = 0.5;
    const double weight_via_number = 4.0;
    const double weight_short_area = 500.0;
    //
    // 层索引从 0 开始，默认从索引 1（通常为 M2）布线，低层通过引脚补丁接入。
    const int min_routing_layer = 1;
    // 控制资源快耗尽时惩罚上升的快慢；具体公式在 GridGraph::logistic。
    const double cost_logistic_slope = 1.0;
    // 允许绕行支线增加的长度相对主干长度的比例，0.25 表示约四分之一。
    const double max_detour_ratio = 0.25; // allowed stem length increase to trunk length ratio
    // 用作采样目标，控制候选绕行密度；不意味着每条线网一定生成正好 20 条。
    const int target_detour_count = 20;
    // 为过孔附近满足最小金属面积的导线需求乘一个估计系数。
    const double via_multiplier = 2.0;
    //
    // 迷宫搜索使用聚合后的二维代价，另设拥塞惩罚斜率。
    const double maze_logistic_slope = 0.5;
    //
    // 以下值控制输出 guide 的补充区域：资源低于阈值时，给详细布线更多选择。
    // padding 的单位是网格个数，不是微米；inflation_rate 是阈值的递增倍率。
    const double pin_patch_threshold = 20.0;
    const int pin_patch_padding = 1;
    const double wire_patch_threshold = 2.0;
    const double wire_patch_inflation_rate = 1.2;
    //
    // bool 只有 true/false；这里默认不导出容量余额热力图。
    const bool write_heatmap = false;
    
    // 单 die 选项及 H3D 的双 DEF/HBT 配置/输出目录；历史 RRR 选项不在此实现。
    Parameters(int argc, char* argv[]) {
        // 构造函数与类型同名、没有返回类型；创建对象时自动执行。
        if (argc <= 1) {
            log() << "Too few args..." << std::endl;
            exit(1);
        }
        // for(起始;继续条件;每轮变化)：从第 1 项扫描，跳过第 0 项程序名。
        // i++ 是用完当前值后加 1；下面 ++i 则先加 1 再取值。
        for (int i = 1; i < argc; i++) {
            if ((strcmp(argv[i], "-lef") == 0 || strcmp(argv[i], "-def") == 0 ||
                 strcmp(argv[i], "-output") == 0 || strcmp(argv[i], "-threads") == 0 ||
                 strcmp(argv[i], "-top-def") == 0 || strcmp(argv[i], "-bottom-def") == 0 ||
                 strcmp(argv[i], "-hbt-config") == 0 || strcmp(argv[i], "-output-dir") == 0 ||
                 strcmp(argv[i], "-3d-nets") == 0 || strcmp(argv[i], "-net-selection") == 0 ||
                  strcmp(argv[i], "-h3d-method") == 0 || strcmp(argv[i], "-h3d-seed") == 0 ||
                  strcmp(argv[i], "-h3d-paper-ties") == 0 || strcmp(argv[i], "-h3d-assignment-trials") == 0) && i + 1 >= argc) {
                throw std::invalid_argument(std::string("Missing value for ") + argv[i]);
            }
            // strcmp 返回 0 才表示两段字符相等；不是“0 表示不相等”。
            if (strcmp(argv[i], "-lef") == 0) {
                // -lef 后一项才是路径；++i 跳过选项，读取紧随其后的文字。
                // 本代码假定这些选项后确实提供了值，未在这里逐项做边界检查。
                lef_file = argv[++i];
            } else if (strcmp(argv[i], "-def") == 0) {
                def_file = argv[++i];
            } else if (strcmp(argv[i], "-output") == 0) {
                out_file = argv[++i];
            } else if (strcmp(argv[i], "-threads") == 0) {
                // stoi 把例如 "8" 的文字转换为整数 8；这与直接保存字符串不同。
                threads = std::stoi(argv[++i]);
            } else if (strcmp(argv[i], "-top-def") == 0) {
                top_def_file = argv[++i];
            } else if (strcmp(argv[i], "-bottom-def") == 0) {
                bottom_def_file = argv[++i];
            } else if (strcmp(argv[i], "-hbt-config") == 0) {
                hbt_config_file = argv[++i];
            } else if (strcmp(argv[i], "-output-dir") == 0) {
                output_dir = argv[++i];
            } else if (strcmp(argv[i], "-3d-nets") == 0) {
                h3d_netlist_file = argv[++i];
            } else if (strcmp(argv[i], "-net-selection") == 0) {
                h3d_net_selection_file = argv[++i];
            } else if (strcmp(argv[i], "-h3d-init-only") == 0) {
                h3d_init_only = true;
            } else if (strcmp(argv[i], "-h3d-topology-only") == 0) {
                h3d_topology_only = true;
            } else if (strcmp(argv[i], "-h3d-method") == 0) {
                h3d_method = argv[++i];
            } else if (strcmp(argv[i], "-h3d-paper-ties") == 0) {
                h3d_paper_ties = argv[++i];
            } else if (strcmp(argv[i], "-h3d-assignment-trials") == 0) {
                h3d_assignment_trials = std::stoi(argv[++i]);
            } else if (strcmp(argv[i], "-h3d-seed") == 0) {
                auto seed = std::stoull(argv[++i]);
                if (seed > std::numeric_limits<std::uint32_t>::max()) throw std::invalid_argument("H3D seed exceeds uint32 range");
                h3d_seed = static_cast<std::uint32_t>(seed);
            } else {
                log() << "Unrecognized arg..." << std::endl;
                log() << argv[i] << std::endl;
            }
        }
        if (lef_file.empty()) throw std::invalid_argument("Specify -lef");
        if (h3d_method != "paper" && h3d_method != "enhanced") throw std::invalid_argument("-h3d-method must be paper or enhanced");
        if (h3d_paper_ties != "first" && h3d_paper_ties != "coalesce") throw std::invalid_argument("-h3d-paper-ties must be first or coalesce");
        if (h3d_assignment_trials < 1 || h3d_assignment_trials > 1024) throw std::invalid_argument("-h3d-assignment-trials must be in [1,1024]");
        if (h3d_init_only && h3d_topology_only) throw std::invalid_argument("Choose one H3D diagnostic mode");
        if (isH3D()) {
            if (top_def_file.empty() || bottom_def_file.empty() || hbt_config_file.empty() || output_dir.empty()) {
                throw std::invalid_argument("H3D requires -top-def, -bottom-def, -hbt-config and -output-dir");
            }
            if (!def_file.empty()) throw std::invalid_argument("Use either -def or the two H3D DEF inputs");
        } else if (def_file.empty() || out_file.empty()) {
            throw std::invalid_argument("Single-die routing requires -def and -output");
        }
        log() << "lef file: " << lef_file << std::endl;
        log() << "def file: " << def_file << std::endl;
        log() << "output  : " << out_file << std::endl;
        log() << "threads : " << threads  << std::endl;
        log() << std::endl;
    }
};
