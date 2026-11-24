#pragma once
#include "obj/Design.h"
namespace native3d {
// 补充 Rsyn 当前读取器未注册的 DEF BLOCKAGES 回调。
std::vector<BoxOnLayer> routingBlockages(const Design&,const std::string& path);
}
