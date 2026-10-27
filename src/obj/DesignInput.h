// 独立的 LEF/DEF 导入上下文；多个 die 不共享 Session 的逻辑/物理数据库。
#pragma once
#include "global.h"
#include "rsyn/io/reader/PopulateRsyn.h"

class DesignInput : private Rsyn::PopulateRsyn {
public:
    DesignInput(const std::string& lef, const std::string& def);
    Rsyn::Design logical;
    Rsyn::PhysicalDesign physical;
    LefDscp library;
    DefDscp placement;
};
