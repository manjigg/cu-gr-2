#include "DesignInput.h"
#include "rsyn/io/parser/lef_def/LEFControlParser.h"
#include "rsyn/io/parser/lef_def/DEFControlParser.h"
#include <stdexcept>

DesignInput::DesignInput(const std::string& lef, const std::string& def) {
    if (!std::ifstream(lef) || !std::ifstream(def)) {
        throw std::runtime_error("Cannot open LEF/DEF input: " + lef + " / " + def);
    }
    LEFControlParser().parseLEF(lef, library);
    DEFControlParser().parseDEF(def, placement);
    if (!placement.clsHasDatabaseUnits || !placement.clsHasDieBounds || library.clsLefLayer.empty()) {
        throw std::runtime_error("LEF/DEF input is missing units, die bounds, or layers");
    }
    if (placement.clsDatabaseUnits <= 0 || !library.clsLefUnitsDscp.clsHasDatabase ||
        library.clsLefUnitsDscp.clsDatabase != placement.clsDatabaseUnits ||
        placement.clsDieBounds[LOWER][X] >= placement.clsDieBounds[UPPER][X] ||
        placement.clsDieBounds[LOWER][Y] >= placement.clsDieBounds[UPPER][Y]) {
        throw std::invalid_argument("LEF and DEF must have equal positive DBU units and valid die bounds");
    }
    logical = Rsyn::Design(placement.clsDesignName);
    populateRsyn(library, placement, logical);
    physical.initPhysicalDesign(logical, {{"clsEnableMergeRectangles", false},
                                        {"clsEnableNetPinBoundaries", true}});
    physical.loadLibrary(library);
    physical.loadDesign(placement);
}
