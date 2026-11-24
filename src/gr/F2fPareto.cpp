#include "F2fPareto.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace {
bool finiteMetrics(const F2fParetoMetrics& m) {
    return std::isfinite(m.wireLengthUm) &&
           std::isfinite(m.wireOverflowDelta) &&
           std::isfinite(m.resourceOverflowDelta) &&
           std::isfinite(m.congestionCost) &&
           m.hbtCount >= 0 && m.viaCount >= 0;
}

bool noWorse(double a, double b, double eps) {
    return a <= b + eps;
}

bool strictlyBetter(double a, double b, double eps) {
    return a < b - eps;
}
} // namespace

bool f2fParetoDominates(const F2fParetoMetrics& a,
                        const F2fParetoMetrics& b,
                        double epsilon) {
    if (!finiteMetrics(a) || !finiteMetrics(b)) return false;

    const bool allNoWorse =
        noWorse(a.wireLengthUm, b.wireLengthUm, epsilon) &&
        a.hbtCount <= b.hbtCount &&
        a.viaCount <= b.viaCount &&
        noWorse(a.wireOverflowDelta, b.wireOverflowDelta, epsilon) &&
        noWorse(a.resourceOverflowDelta, b.resourceOverflowDelta, epsilon) &&
        noWorse(a.congestionCost, b.congestionCost, epsilon);

    const bool atLeastOneBetter =
        strictlyBetter(a.wireLengthUm, b.wireLengthUm, epsilon) ||
        a.hbtCount < b.hbtCount ||
        a.viaCount < b.viaCount ||
        strictlyBetter(a.wireOverflowDelta, b.wireOverflowDelta, epsilon) ||
        strictlyBetter(a.resourceOverflowDelta, b.resourceOverflowDelta, epsilon) ||
        strictlyBetter(a.congestionCost, b.congestionCost, epsilon);

    return allNoWorse && atLeastOneBetter;
}

bool insertF2fParetoCandidate(std::vector<F2fParetoRecord>& archive,
                              F2fParetoRecord candidate,
                              double epsilon) {
    if (!finiteMetrics(candidate.metrics)) return false;

    for (const auto& current : archive) {
        const bool sameMetrics =
            current.metrics.wireLengthUm == candidate.metrics.wireLengthUm &&
            current.metrics.hbtCount == candidate.metrics.hbtCount &&
            current.metrics.viaCount == candidate.metrics.viaCount &&
            current.metrics.wireOverflowDelta == candidate.metrics.wireOverflowDelta &&
            current.metrics.resourceOverflowDelta == candidate.metrics.resourceOverflowDelta &&
            current.metrics.congestionCost == candidate.metrics.congestionCost;
        if (sameMetrics && current.sites == candidate.sites) return false;

        if (f2fParetoDominates(current.metrics, candidate.metrics, epsilon)) return false;
    }

    archive.erase(
        std::remove_if(archive.begin(), archive.end(),
                       [&](const F2fParetoRecord& current) {
                           return f2fParetoDominates(candidate.metrics,
                                                     current.metrics, epsilon);
                       }),
        archive.end());

    archive.push_back(std::move(candidate));
    std::stable_sort(archive.begin(), archive.end(),
                     [](const F2fParetoRecord& a, const F2fParetoRecord& b) {
                         if (a.metrics.hbtCount != b.metrics.hbtCount)
                             return a.metrics.hbtCount < b.metrics.hbtCount;
                         if (a.metrics.wireLengthUm != b.metrics.wireLengthUm)
                             return a.metrics.wireLengthUm < b.metrics.wireLengthUm;
                         if (a.metrics.viaCount != b.metrics.viaCount)
                             return a.metrics.viaCount < b.metrics.viaCount;
                         return a.sites < b.sites;
                     });
    return true;
}
