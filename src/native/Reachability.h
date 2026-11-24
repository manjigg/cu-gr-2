#pragma once
#include "RoutingGraph.h"
namespace native3d {
// 对空载物理资源图求连通分量。一个 net 的各 pin 若无共同分量，可证明该模型不可行。
Json checkStaticReachability(const RoutingGraph&);
}
