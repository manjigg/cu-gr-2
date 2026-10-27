// 文件功能：CUGR 全局布线程序的入口，串联参数解析、设计读取、布线和 guide 输出。
// 阅读顺序：Parameters -> Design -> GlobalRouter::route() -> GlobalRouter::write()。
//
// 【零基础先看这里】本文件像工厂的总开关，本身不计算每条导线怎样走。
// 输入：你在终端输入的命令，例如 ./route -lef a.lef -def a.def -output a.guide。
// 过程：把文件名装进参数对象；读取芯片资料；创建布线器；计算路线；保存结果。
// 输出：guide 文件，它告诉后续详细布线器“每条线可以优先在哪些区域走”。
// 类比：Parameters 是订单，Design 是地图，GlobalRouter 是规划路线的工作人员。
// 注意“创建对象”也可以做工作：Design 的构造函数会立即读取输入文件。
// 因而读这里时，不仅要看显式调用的 route()，也要看各对象的构造函数。
// 下方保留完整执行顺序，并在每个步骤旁解释 C++ 写法。
#include "global.h"
// #include 可理解为“引入别的文件提供的定义”，不是现在就执行那个文件。
// .h 通常说明有哪些类型/方法，.cpp 通常写方法具体怎样完成工作。
#include "obj/Design.h"
#include "gr/GlobalRouter.h"
#include "h3d/H3DGlobalRouter.h"

// main 是操作系统启动程序时首先进入的函数。
// int 表示函数结束可返回整数状态；到 main 末尾未写 return 时相当于返回 0。
// argc 是命令行项目总数，argv 是逐项文字；argv[0] 通常是程序名。
// char* 是“指向字符的地址”，此处每个 argv[i] 可以先理解为一段命令行文字。
// 花括号 { } 围出函数工作区；每条以 ; 结尾的语句按先后顺序执行。
int main(int argc, char* argv[]) {
    try {
    // 【变量对照】argc 参数项数，argv[i] 第 i 项文字；parameters 是解析后的参数对象。
    // design 是芯片资料持有者，globalRouter 是调度器。三者按定义顺序创建、反序销毁。
    // 返回值 0 表示正常进程状态，不是“零条线网”或“零溢出”。
    logeol(2);
    // << 在此表示把文字送入输出流；std::endl 表示换行并刷新输出。
    // log() 已经加上时间戳，所以我们能知道各步骤大致在什么时刻发生。
    log() << "GLOBAL ROUTER CUGR" << std::endl;
    logeol(2);
    // Parse parameters
    // “类型 变量名(参数)”：创建 Parameters 类型的对象，取名 parameters。
    // 同名构造函数 Parameters(...) 会被自动调用，将命令行文字转为字段。
    Parameters parameters(argc, argv);
    if (parameters.isH3D()) {
        h3d::routeH3D(parameters);
        return 0;
    }
    
    // Read LEF/DEF
    // 把上一步的参数交给 Design。该对象拥有层、实例、线网、障碍等资料。
    // 执行完这一行，芯片设计已经读进内存；这里还没有开始选布线路径。
    Design design(parameters);
    
    // Global router
    // GlobalRouter globalRouter(...)：创建一个全局布线器，把芯片设计和运行参数交给它。
    // globalRouter.route()：让布线器真正开始布线。这是我们后面最重要的分析入口。
    // globalRouter.write()：把计算好的布线结果写出去。
    // 创建布线器时先建立容量网格，把物理引脚转换成可连接的网格位置。
    // 小写 globalRouter 是对象名；大写 GlobalRouter 是这个对象的类型。
    GlobalRouter globalRouter(design, parameters);
    // 点号 . 表示“调用这个对象的方法”。route() 才执行三阶段路径计算。
    globalRouter.route();
    // 计算结果先在内存中，write() 负责生成 guide 文本并写入磁盘。
    globalRouter.write();
    
    logeol();
    log() << "Terminated." << std::endl;
    loghline();
    // 最后打印当前/峰值内存，帮助观察读取和路由过程中资源用了多少。
    logmem();
    logeol();
    } catch (const std::exception& error) {
        std::cerr << "Routing failed: " << error.what() << std::endl;
        return 1;
    }
}
