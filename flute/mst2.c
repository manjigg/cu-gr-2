// 文件功能：结合八方向最近邻与二叉堆构建二维点集最小生成树，输出各点 parent 索引。
// 包初始化/清理函数负责分配与释放算法复用的近邻及堆工作空间。
// 【零基础阅读】先找候选近邻，再用最小堆逐步把低价新点并入已连接区域。
// parent 记录每个点通过哪个已接入点连进树，最终无环地连通输入点集。
// 最小生成树只使用现有点，FLUTE 高阶流程还会据它分解/改进为 Steiner 拓扑。
#include  <stdlib.h>
#include  <stdio.h>
#include   <assert.h>
#include  "global.h"
// n 点数，pt 点数组，parent 输出连接编号；近邻候选和堆 key 决定下一次接入哪个点。
#include  "neighbors.h"
#include  "dist.h"
#include  "heap.h"
#include  "err.h"



void  mst2_package_init( long  n )
{
  allocate_heap( n );
  allocate_nn_arrays( n );
}

/****************************************************************************/
/*
*/

void  mst2_package_done()
{
  deallocate_heap();
  deallocate_nn_arrays();
}  

/****************************************************************************/
/*
*/

void  mst2
( 
  long    n,
  Point*  pt, 
  long*   parent
)
{
  long  i, k, nn1;
  long  d;
  long  oct;
  long  root = 0;
  extern  nn_array*  nn;

//  brute_force_nearest_neighbors( n, pt, nn );
  dq_nearest_neighbors( n, pt, nn );

  /* 
     Binary heap implementation of Prim's algorithm.
     Runs in O(n*log(n)) time since at most 8n edges are considered
  */

  heap_init( n );
  heap_insert( root, 0 );
  parent[root] = root;

  for( k = 0;  k < n;  k++ )   /* n points to be extracted from heap */
  {
    i = heap_delete_min();

    if (i<0) break;
#ifdef DEBUG
    assert( i >= 0 );
#endif 

    /*
      pt[i] entered the tree, update heap keys for its neighbors
    */
    for( oct = 0;  oct < 8;  oct++ )
    {
      nn1 = nn[i][oct]; 
      if( nn1 >= 0 )
      {
        d  = dist( pt[i], pt[nn1] );
        if( in_heap(nn1) && (d < heap_key(nn1)) )
        {
          heap_decrease_key( nn1, d );
          parent[nn1] = i;
        } 
        else if( never_seen(nn1) )
        {
          heap_insert( nn1, d );
          parent[nn1] = i;
        }
      }
    }
  }
}

/****************************************************************************/
/****************************************************************************/
