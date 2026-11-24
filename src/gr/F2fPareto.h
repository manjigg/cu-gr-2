#pragma once
#include <vector>

struct F2fParetoMetrics {
    double wireLengthUm = 0.0;
    int hbtCount = 0;
    int viaCount = 0;
    double wireOverflowDelta = 0.0;
    double resourceOverflowDelta = 0.0;
    double congestionCost = 0.0;
};

struct F2fParetoRecord {
    F2fParetoMetrics metrics;
    std::vector<int> sites;
    int stage = 0;
};

bool f2fParetoDominates(const F2fParetoMetrics& a,
                        const F2fParetoMetrics& b,
                        double epsilon = 1e-9);

bool insertF2fParetoCandidate(std::vector<F2fParetoRecord>& archive,
                              F2fParetoRecord candidate,
                              double epsilon = 1e-9);
