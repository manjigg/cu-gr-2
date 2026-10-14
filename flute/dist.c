// 文件功能：实现二维整数点之间的曼哈顿距离及指针参数版本，供最近邻与 MST 比较边长。
// 【零基础阅读】比较 x/y 差值的绝对值再相加，得到直角路线最基本的距离尺度。
// 指针版本通过 -> 读坐标，值版本通过 . 读坐标；数学含义相同。
// 此距离未计金属层、过孔或容量，只供二维 MST/近邻辅助比较。
#include "global.h"
// p/q 端点；坐标差先取非负绝对值再相加，不计算层变化/拥塞/过孔成本。

/*********************************************************************/
/*
   Return the Manhattan distance between two points
*/

long  dist(
  Point  p,
  Point  q
)
{
  long  dx, dy;
    
  dx = (p.x) - (q.x);
  if( dx < 0 )  dx = -dx;
  dy = (p.y) - (q.y);
  if( dy < 0 )  dy = -dy;

  return  dx + dy; 
}

/*********************************************************************/
/*
   Return the Manhattan distance between two points
*/

long  dist2(
  Point*  p,
  Point*  q
)
{
  long  dx, dy;
    
  dx = (p->x) - (q->x);
  if( dx < 0 )  dx = -dx;
  dy = (p->y) - (q->y);
  if( dy < 0 )  dy = -dy;

  return  dx + dy; 
}

/*********************************************************************/
/*********************************************************************/
