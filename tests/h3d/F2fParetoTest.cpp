#include "../../src/gr/F2fPareto.h"
#include <cassert>
#include <limits>

int main() {
    std::vector<F2fParetoRecord> archive;

    F2fParetoRecord a;
    a.metrics = {1200.0, 3, 150, 0.0, 0.0, 10.0};
    a.sites = {1, 2};
    assert(insertF2fParetoCandidate(archive, a));

    F2fParetoRecord b;
    b.metrics = {1050.0, 4, 140, 0.0, 0.0, 9.0};
    b.sites = {3, 4, 5};
    assert(insertF2fParetoCandidate(archive, b));
    assert(archive.size() == 2);

    F2fParetoRecord dominated;
    dominated.metrics = {1300.0, 5, 160, 0.0, 0.0, 12.0};
    dominated.sites = {6, 7, 8, 9};
    assert(!insertF2fParetoCandidate(archive, dominated));
    assert(archive.size() == 2);

    F2fParetoRecord better;
    better.metrics = {1000.0, 3, 130, 0.0, 0.0, 8.0};
    better.sites = {10, 11, 12};
    assert(insertF2fParetoCandidate(archive, better));
    assert(archive.size() == 1);
    assert(archive.front().sites == better.sites);

    F2fParetoRecord sameMetricsDifferentSites = better;
    sameMetricsDifferentSites.sites = {13, 14, 15};
    assert(insertF2fParetoCandidate(archive, sameMetricsDifferentSites));
    assert(archive.size() == 2);

    F2fParetoRecord invalid;
    invalid.metrics = {std::numeric_limits<double>::infinity(), 1, 1, 0, 0, 0};
    assert(!insertF2fParetoCandidate(archive, invalid));
    return 0;
}
