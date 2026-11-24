#pragma once
#include "global.h"
#include "GeoTypes.h"

class Macro {
    friend class boost::serialization::access;
    Macro() = default;
    template<class Archive> void serialize(Archive& ar, unsigned) {
        ar & name & orientation & index & pinNames & pins & obstacles;
        if (Archive::is_loading::value) {
            for (size_t i = 0; i < pinNames.size(); ++i) pinIndices.emplace(pinNames[i], i);
        }
    }
    // Macros of a SPECIFIC orientation (NOT the original macro)
public:
    Macro(int idx, const Rsyn::Cell& cell, const Rsyn::PhysicalDesign& physicalDesign);
    const std::string& getPinName(int i) const { return pinNames.at(i); }
    int getPinIndex(std::string pinName);
    const vector<BoxOnLayer>& getObstacles() const { return obstacles; }
    const vector<BoxOnLayer>& getPinShapes(int pinIndex) const { return pins[pinIndex]; }
    
private:
    std::string name;
    int orientation;
    int index;
    robin_hood::unordered_map<std::string, int> pinIndices;
    vector<std::string> pinNames;
    vector<vector<BoxOnLayer>> pins;
    vector<BoxOnLayer> obstacles;
};

class Instance {
    friend class boost::serialization::access;
    Instance() = default;
    template<class Archive> void serialize(Archive& ar, unsigned) {
        ar & index & name & type;
        if (isCell()) ar & macroIndex & position.x & position.y;
        else ar & port;
    }
public:
    const static int CELL = 0;
    const static int PORT = 1;
    
    Instance(int idx, std::string _name, int macroIndex, utils::PointT<DBU> pos): 
        index(idx), name(_name), macroIndex(macroIndex), position(pos), type(CELL) {}
    Instance(int idx, std::string _name, const Rsyn::PhysicalPort& phyPort);
    const std::string& getName() const { return name; }
    bool isCell() const { return type == CELL; }
    int getMacroIndex() const { return macroIndex; }
    utils::PointT<DBU> getPosition() const { return position; }
    const BoxOnLayer& getPort() const { return port; }
    
private:
    std::string name;
    int index;
    int type;
    // For cells
    utils::PointT<DBU> position;
    int macroIndex;
    // for ports
    BoxOnLayer port; 
};