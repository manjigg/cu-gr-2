// 文件功能：声明 MST 二叉堆的数据结构和分配、初始化、插入、降键、删除最小值接口。
// 【零基础阅读】最小堆像可随时取最低价候选的队列，不保证所有内部元素完全排序。
// key 是候选边分，顶点/索引记录让 decrease_key 能定位已有候选并降价。
// Prim 每次从最便宜边扩展树，因此需要插入、降键和取最小值三种操作。
#ifndef  _HEAP_H_
#define  _HEAP_H_

#include "global.h"

struct  heap_info
{
  // key 优先级/候选边分，idx 为堆相关索引，elt 为元素身份；交换堆位置时需同步索引。
  long  key;
  long  idx;
  long  elt;
};

typedef  struct heap_info  Heap;

extern Heap*   _heap;

#define  heap_key( p )     ( _heap[p].key )
#define  heap_idx( p )     ( _heap[p].idx )
#define  heap_elt( k )     ( _heap[k].elt )

#define  in_heap( p )    ( heap_idx(p) > 0 )
#define  never_seen( p ) ( heap_idx(p) == 0 ) 

void   allocate_heap( long n );
void   deallocate_heap();
void   heap_init( long  n );
void   heap_insert( long  p,  long key );
void   heap_decrease_key( long  p,  long new_key );
long   heap_delete_min();

#endif /* _HEAP_H_ */
