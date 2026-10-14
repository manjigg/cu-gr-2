// 文件功能：声明二维 MST 构建 mst2 及其工作空间初始化/清理接口。
// 【零基础阅读】输入 n 个点，输出 parent 数组，parent[i] 说明树上点 i 的连接关系。
// 初始化先准备近邻/堆工作空间，完成后清理，可在同一包生命周期内复用。
// 本头文件声明流程，mst2.c 的 Prim 循环才实际连接各点。
#ifndef _MST2_H_
#define _MST2_H_

#include "global.h"
// n 为点数，pt 为 Point 数组，parent[i] 为点 i 在生成树的连接父编号；不是库单元父模块。

void  mst2_package_init( long  n );
void  mst2_package_done();
void  mst2( long n, Point* pt, long* parent );

#endif 
