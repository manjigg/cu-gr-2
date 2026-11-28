#pragma once
#include "global.h"
#include <boost/serialization/access.hpp>

utils::BoxT<DBU> getBoxFromRsynBounds(const Bounds& bounds);

class BoxOnLayer: public utils::BoxT<DBU> {
public:
    int layerIdx;
    template<class Archive> void serialize(Archive& ar, unsigned) {
        ar & layerIdx & x.low & x.high & y.low & y.high;
    }

    //  constructors
    BoxOnLayer() : layerIdx(-1) {}
    template <typename... Args>
    BoxOnLayer(int layerIndex, Args... params) : utils::BoxT<DBU>(params...), layerIdx(layerIndex) {}

    // inherit setters from utils::BoxT in batch
    void Set() { Set(-1); }
    template <typename... Args>
    void Set(int layerIndex, Args... params) {
        layerIdx = layerIndex;
        utils::BoxT<DBU>::Set(params...);
    }

    bool isConnected(const BoxOnLayer& rhs) const;

    friend ostream& operator<<(ostream& os, const BoxOnLayer& box);
};