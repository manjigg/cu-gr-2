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
    template <typename... Args>
    BoxOnLayer(int layerIndex = -1, Args... params) : layerIdx(layerIndex), utils::BoxT<DBU>(params...) {}

    // inherit setters from utils::BoxT in batch
    template <typename... Args>
    void Set(int layerIndex = -1, Args... params) {
        layerIdx = layerIndex;
        utils::BoxT<DBU>::Set(params...);
    }

    bool isConnected(const BoxOnLayer& rhs) const;

    friend ostream& operator<<(ostream& os, const BoxOnLayer& box);
};