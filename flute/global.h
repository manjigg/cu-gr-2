// 文件功能：定义 FLUTE 最近邻/MST 辅助算法的整数 Point、八方向近邻数组及公共常量。
// 【零基础阅读】Point 是 x/y 两个 long 整数，nn_array[8] 给八个方向的近邻各留一个槽。
// 这是 FLUTE 辅助算法自己的点类型，与 CUGR 的 GRPoint（金属层+网格位置）不同。
// TRUE/FALSE 等宏是 C 风格常量，定义它们不触发最近邻计算。
#ifndef  _GLOBAL_H_
#define  _GLOBAL_H_

#include <stdio.h>

#define  TRUE   1
#define  FALSE  0
#define  MAXLONG  0x7fffffffL

struct point
{
    // x/y 二维整数坐标；Point 不含真实金属层，nn_array 的八槽分别定位方向区域近邻。
  long    x, y;
};

typedef  struct point Point;

typedef  long nn_array[8];

#endif  /* _GLOBAL_H_ */
