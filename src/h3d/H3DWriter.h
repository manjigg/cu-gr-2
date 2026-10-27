#pragma once
#include "SubnetDecomposer.h"

namespace h3d {
// 保留输入 DEF 的其他段，仅重建信号 NETS 并更新/添加 PINS。
void writeDieDef(const std::string& input, const std::string& output, const H3DDesign& design,
                 int die, const std::vector<Subnet>& subnets);
} // namespace h3d
