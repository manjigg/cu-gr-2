#pragma once
#include "H3DDesign.h"

namespace h3d {
// 只观测现有导入、FLUTE、数量 DP；不改变任何代价、候选或终端位置。
bool runH3DDiagnostics(const Parameters& parameters, const H3DDesign& design, double initializationSeconds);
} // namespace h3d
