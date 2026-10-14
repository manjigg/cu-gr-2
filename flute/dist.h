// 文件功能：声明 FLUTE 辅助模块的点距离接口 dist 和 dist2。
// 【零基础阅读】曼哈顿距离只许横竖走，点 (1,2) 到 (4,6) 距离是 3+4=7。
// dist 传点值，dist2 传点地址；两种接口适应不同调用方式。
// 这里声明函数，真正相减/取绝对值的计算在 dist.c。
#ifndef _DIST_H_
#define _DIST_H_

#include "global.h"
// p/q 是两个点，指针版本 p/q 为点地址；返回 |p.x-q.x|+|p.y-q.y|，单位同输入坐标。

long  dist(
  Point  p,
  Point  q
);

long  dist2(   
  Point*  p,
  Point*  q
);

#endif
