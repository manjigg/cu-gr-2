// 文件功能：声明八方向最近邻计算的暴力/分治接口及辅助数组生命周期函数。
// 【零基础阅读】把点周围分成八个方向区域，为每个区域寻找近邻候选。
// 暴力方式逐点比较，分治方式拆分点集并复用排序/辅助数组以减少比较。
// 输入是二维点，输出近邻关系；最终选哪些边还由 MST/高阶构树算法决定。
#include "global.h"
// n 点数，pt 点数组，nn[i][方向槽] 最近邻点编号；八方向槽不是八个金属层。

void  allocate_nn_arrays( long n );
void  deallocate_nn_arrays();

void  brute_force_nearest_neighbors
(
  long       n,
  Point*     pt,
  nn_array*  nn
);

void  dq_nearest_neighbors
(
  long       n,
  Point*     pt,
  nn_array*  nn
);
