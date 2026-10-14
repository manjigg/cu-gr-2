// 文件功能：线网模块的编译单元入口；当前仅包含 Net.h，方法均在头文件中内联实现。
// 【新手阅读】文件短并不是遗漏：Net 的构造、添加引脚和查询全写在 Net.h 中。
// #include 让编译器读到那些定义；本文件没有额外算法，也不在运行时读取头文件。
// 想知道 Net 的实际字段与作用，请跟到 Net.h 的 PinReference 和 class Net。
#include "Net.h"
// 无新的成员/局部变量；index/name/pinRefs、instanceIndex/pinIndex 的定义和解释都在 Net.h。
