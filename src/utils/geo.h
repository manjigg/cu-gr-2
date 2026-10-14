// 文件功能：提供 PointT/IntervalT/BoxT/SegmentT 几何模板及距离、矩形合并和多边形切片。
// 默认区间为无效空区间；Update 可逐点扩大范围，区分包含边界与严格相交的查询。
//
// 【白话职责】提供“点在哪里、区间覆盖多少、矩形是否相交”等算法通用小积木。
// template<typename T> 是同一套代码适用于不同数字：PointT<int> 存网格编号，
// PointT<DBU> 存物理整数位置，PointT<double> 可以存带小数的位置。
// PointT 有 x/y；IntervalT 有 low/high；BoxT 用 x/y 两个区间表达矩形。
// 例：BoxT(1,2,5,7) 的宽=4、高=5、半周长=9、面积=20。
// 区间 [2,5] 的长度 range=3，但包含整数 2/3/4/5 共 4 个点，二者不同。
// “相交”包含端点相接，“严格相交”要求有正长度/面积，查询时要分清。
// 文件后段 MergeRects/SlicePolygons 是合并覆盖区域的小算法，不负责路线优化。
//
// Some class templates for geometry primitives (point, interval, box)
//

#pragma once

#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>
#include <algorithm>

namespace utils {

// 点坐标既可用物理 DBU，也可用整数网格索引；由调用方的模板参数决定。
// Point template
template <typename T>
class PointT {
public:
    // T 数字类型；x/y 实际分量；xx/yy 构造传入值；d 下标选择轴；rhs 运算右侧点。
    // divisor 除数；os 输出流，pt 待打印点。单位由 PointT<int/DBU/...> 的使用方约定。
    T x, y;
    PointT(T xx = std::numeric_limits<T>::has_infinity ? std::numeric_limits<T>::infinity()
                                                       : std::numeric_limits<T>::max(),
           T yy = std::numeric_limits<T>::has_infinity ? std::numeric_limits<T>::infinity()
                                                       : std::numeric_limits<T>::max())
        : x(xx), y(yy) {}
    bool IsValid() { return *this != PointT(); }

    // Operators
    const T& operator[](const unsigned d) const {
        assert(d == 0 || d == 1);
        return (d == 0 ? x : y);
    }
    T& operator[](const unsigned d) {
        assert(d == 0 || d == 1);
        return (d == 0 ? x : y);
    }
    PointT operator+(const PointT& rhs) { return PointT(x + rhs.x, y + rhs.y); }
    PointT operator/(T divisor) { return PointT(x / divisor, y / divisor); }
    PointT& operator+=(const PointT& rhs) {
        x += rhs.x;
        y += rhs.y;
        return *this;
    }
    PointT& operator-=(const PointT& rhs) {
        x -= rhs.x;
        y -= rhs.y;
        return *this;
    }
    bool operator==(const PointT& rhs) const { return x == rhs.x && y == rhs.y; }
    bool operator!=(const PointT& rhs) const { return !(*this == rhs); }

    friend inline std::ostream& operator<<(std::ostream& os, const PointT& pt) {
        os << "(" << pt.x << ", " << pt.y << ")";
        return os;
    }
};

// L-1 (Manhattan) distance between points
template <typename T>
inline T Dist(const PointT<T>& pt1, const PointT<T>& pt2) {
    return std::abs(pt1.x - pt2.x) + std::abs(pt1.y - pt2.y);
}

// L-2 (Euclidean) distance between points
template <typename T>
inline double L2Dist(const PointT<T>& pt1, const PointT<T>& pt2) {
    return std::sqrt(std::pow(pt1.x - pt2.x, 2) + std::pow(pt1.y - pt2.y, 2));
}

// L-inf distance between points
template <typename T>
inline T LInfDist(const PointT<T>& pt1, const PointT<T>& pt2) {
    return std::max(std::abs(pt1.x - pt2.x), std::abs(pt1.y - pt2.y));
}

// low/high 表达闭区间；range=high-low，若统计整数网格数量则需另加 1。
// Interval template
template <typename T>
class IntervalT {
public:
    // low/high 当前低/高端，lo/hi 传入低/高端，val 单点值，newVal 要纳入区间的新值。
    // rhs 另一待比较/合并区间，params 可变构造输入；range() 是长度，不是整数点数量。
    T low, high;

    template <typename... Args>
    IntervalT(Args... params) {
        Set(params...);
    }

    // Setters
    void Set() {
        // 无参数 Set 建“空区间”：low 很大，high 很小，故 low>high 判为无效。
        // 随后 Update 第一个值时，就能把上下界都更新成这个值。
        low = std::numeric_limits<T>::has_infinity ? std::numeric_limits<T>::infinity() : std::numeric_limits<T>::max();
        high = std::numeric_limits<T>::has_infinity ? -std::numeric_limits<T>::infinity()
                                                    : std::numeric_limits<T>::lowest();
    }
    void Set(T val) {
        low = val;
        high = val;
    }
    void Set(T lo, T hi) {
        low = lo;
        high = hi;
    }

    // Getters
    T center() const { return (high + low) / 2; }
    T range() const { return high - low; }

    // Update
    // Update() is always safe, FastUpdate() assumes existing values
    void Update(T newVal) {
        if (newVal < low) low = newVal;
        if (newVal > high) high = newVal;
    }
    void FastUpdate(T newVal) {
        if (newVal < low)
            low = newVal;
        else if (newVal > high)
            high = newVal;
    }

    // Two types of intervals: 1. normal, 2. degenerated (i.e., point)
    // is valid interval (i.e., valid closed interval)
    bool IsValid() const { return low <= high; }
    // is strictly valid interval (excluding degenerated ones, i.e., valid open interval)
    bool IsStrictValid() const { return low < high; }

    // Geometric Query/Update
    // interval/range of union (not union of intervals)
    IntervalT UnionWith(const IntervalT& rhs) const {
        if (!IsValid())
            return rhs;
        else if (!rhs.IsValid())
            return *this;
        else
            return IntervalT(std::min(low, rhs.low), std::max(high, rhs.high));
    }
    // may return an invalid interval (as empty intersection)
    IntervalT IntersectWith(const IntervalT& rhs) const {
        // 两区间公共部分：左端取较大值，右端取较小值。
        // [1,5] 与 [3,8] 得 [3,5]；若结果 low>high 则没有交集。
        return IntervalT(std::max(low, rhs.low), std::min(high, rhs.high));
    }
    bool HasIntersectWith(const IntervalT& rhs) const { return IntersectWith(rhs).IsValid(); }
    bool HasStrictIntersectWith(const IntervalT& rhs) const { return IntersectWith(rhs).IsStrictValid(); }
    //  Parallel run length between intervals
    T ParaRunLength(const IntervalT& rhs) const { return IntersectWith(rhs).range(); }
    // contain a val
    bool Contain(int val) const { return val >= low && val <= high; }
    bool StrictlyContain(int val) const { return val > low && val < high; }
    // get nearest point(s) to val (assume valid intervals)
    T GetNearestPointTo(T val) const {
        if (val <= low) {
            return low;
        } else if (val >= high) {
            return high;
        } else {
            return val;
        }
    }
    IntervalT GetNearestPointsTo(IntervalT val) const {
        if (val.high <= low) {
            return {low};
        } else if (val.low >= high) {
            return {high};
        } else {
            return IntersectWith(val);
        }
    }

    void ShiftBy(const T& rhs) {
        low += rhs;
        high += rhs;
    }

    // Operators
    bool operator==(const IntervalT& rhs) const {
        return (!IsValid() && !rhs.IsValid()) || (low == rhs.low && high == rhs.high);
    }
    bool operator!=(const IntervalT& rhs) const { return !(*this == rhs); }

    friend inline std::ostream& operator<<(std::ostream& os, const IntervalT<T>& interval) {
        os << "(" << interval.low << ", " << interval.high << ")";
        return os;
    }
};

// Distance between intervals/points (assume valid intervals)
template <typename T>
inline T Dist(const IntervalT<T>& intvl, const T val) {
    return std::abs(intvl.GetNearestPointTo(val) - val);
}

template <typename T>
inline T Dist(const IntervalT<T>& int1, const IntervalT<T>& int2) {
    if (int1.high <= int2.low) {
        return int2.low - int1.high;
    } else if (int1.low >= int2.high) {
        return int1.low - int2.high;
    } else {
        return 0;
    }
}

// 矩形由两个独立区间构成，hp() 是宽+高，用于线网排序的半周长指标。
// Box template
template <typename T>
class BoxT {
public:
    // x/y 两轴区间；lx/ly/hx/hy 是四边界，xRange/yRange 是传入区间，pt 是传入二维点。
    // i 选择轴 0/1；rhs 另一矩形或位移，box 待拷贝矩形；hp 宽+高，area 宽×高。
    IntervalT<T> x, y;

    template <typename... Args>
    BoxT(Args... params) {
        Set(params...);
    }

    // Setters
    T& lx() { return x.low; }
    T& ly() { return y.low; }
    T& hy() { return y.high; }
    T& hx() { return x.high; }
    IntervalT<T>& operator[](unsigned i) {
        assert(i == 0 || i == 1);
        return (i == 0) ? x : y;
    }
    void Set() {
        x.Set();
        y.Set();
    }
    void Set(T xVal, T yVal) {
        x.Set(xVal);
        y.Set(yVal);
    }
    void Set(const PointT<T>& pt) { Set(pt.x, pt.y); }
    void Set(T lx, T ly, T hx, T hy) {
        x.Set(lx, hx);
        y.Set(ly, hy);
    }
    void Set(const IntervalT<T>& xRange, const IntervalT<T>& yRange) {
        x = xRange;
        y = yRange;
    }
    void Set(const PointT<T>& low, const PointT<T>& high) { Set(low.x, low.y, high.x, high.y); }
    void Set(const BoxT<T>& box) { Set(box.x, box.y); }

    // Two types of boxes: normal & degenerated (line or point)
    // is valid box
    bool IsValid() const { return x.IsValid() && y.IsValid(); }
    // is strictly valid box (excluding degenerated ones)
    bool IsStrictValid() const { return x.IsStrictValid() && y.IsStrictValid(); }  // tighter

    // Getters
    T lx() const { return x.low; }
    T ly() const { return y.low; }
    T hy() const { return y.high; }
    T hx() const { return x.high; }
    T cx() const { return x.center(); }
    T cy() const { return y.center(); }
    T width() const { return x.range(); }
    T height() const { return y.range(); }
    T hp() const { return width() + height(); }  // half perimeter
    T area() const { return width() * height(); }
    const IntervalT<T>& operator[](unsigned i) const {
        assert(i == 0 || i == 1);
        return (i == 0) ? x : y;
    }

    // Update() is always safe, FastUpdate() assumes existing values
    void Update(T xVal, T yVal) {
        x.Update(xVal);
        y.Update(yVal);
    }
    void FastUpdate(T xVal, T yVal) {
        x.FastUpdate(xVal);
        y.FastUpdate(yVal);
    }
    void Update(const PointT<T>& pt) { Update(pt.x, pt.y); }
    void FastUpdate(const PointT<T>& pt) { FastUpdate(pt.x, pt.y); }

    // Geometric Query/Update
    BoxT UnionWith(const BoxT& rhs) const { return {x.UnionWith(rhs.x), y.UnionWith(rhs.y)}; }
    BoxT IntersectWith(const BoxT& rhs) const { return {x.IntersectWith(rhs.x), y.IntersectWith(rhs.y)}; }
    bool HasIntersectWith(const BoxT& rhs) const { return IntersectWith(rhs).IsValid(); }
    bool HasStrictIntersectWith(const BoxT& rhs) const { return IntersectWith(rhs).IsStrictValid(); }  // tighter
    bool Contain(const PointT<T>& pt) const { return x.Contain(pt.x) && y.Contain(pt.y); }
    bool StrictlyContain(const PointT<T>& pt) const { return x.StrictlyContain(pt.x) && y.StrictlyContain(pt.y); }
    PointT<T> GetNearestPointTo(const PointT<T>& pt) { return {x.GetNearestPointTo(pt.x), y.GetNearestPointTo(pt.y)}; }
    BoxT GetNearestPointsTo(BoxT val) const { return {x.GetNearestPointsTo(val.x), y.GetNearestPointsTo(val.y)}; }

    void ShiftBy(const PointT<T>& rhs) {
        x.ShiftBy(rhs.x);
        y.ShiftBy(rhs.y);
    }

    bool operator==(const BoxT& rhs) const { return (x == rhs.x) && (y == rhs.y); }
    bool operator!=(const BoxT& rhs) const { return !(*this == rhs); }

    friend inline std::ostream& operator<<(std::ostream& os, const BoxT<T>& box) {
        os << "[x: " << box.x << ", y: " << box.y << "]";
        return os;
    }
};

// L-1 (Manhattan) distance between boxes/points (assume valid boxes)
template <typename T>
inline T Dist(const BoxT<T>& box, const PointT<T>& point) {
    return Dist(box.x, point.x) + Dist(box.y, point.y);
}
template <typename T>
inline T Dist(const BoxT<T>& box1, const BoxT<T>& box2) {
    return Dist(box1.x, box2.x) + Dist(box1.y, box2.y);
}

// L-2 (Euclidean) distance between boxes
template <typename T>
inline double L2Dist(const BoxT<T>& box1, const BoxT<T>& box2) {
    return std::sqrt(std::pow(Dist(box1.x, box2.x), 2) + std::pow(Dist(box1.y, box2.y), 2));
}

// L-Inf (max) distance between boxes
template <typename T>
inline T LInfDist(const BoxT<T>& box1, const BoxT<T>& box2) {
    return std::max(Dist(box1.x, box2.x), Dist(box1.y, box2.y));
}

//  Parallel run length between boxes
template <typename T>
inline T ParaRunLength(const BoxT<T>& box1, const BoxT<T>& box2) {
    return std::max(box1.x.ParaRunLength(box2.x), box1.y.ParaRunLength(box2.y));
}

// Merge/stitch overlapped rectangles along mergeDir
// mergeDir: 0 for x/vertical, 1 for y/horizontal
// use BoxT instead of T & BoxT<T> to make it more general
template <typename BoxT>
void MergeRects(std::vector<BoxT>& boxes, int mergeDir) {
    // boxes 输入输出矩形列表，mergeDir 合并轴，boundaryDir 另一轴；lhs/rhs 排序比较两矩形。
    // mergedBoxes 输出缓存，lastBox 最近合并结果，slicedBox 当前输入矩形，i 输入列表下标。
    // 先排序让相邻且对齐的矩形靠在一起，再逐个与上一个已合并矩形比较。
    // 另一轴区间相同才可沿 mergeDir 拼接，避免把 L 形区域误变成完整大矩形。
    int boundaryDir = 1 - mergeDir;
    std::sort(boxes.begin(), boxes.end(), [&](const BoxT& lhs, const BoxT& rhs) {
        return lhs[boundaryDir].low < rhs[boundaryDir].low ||
               (lhs[boundaryDir].low == rhs[boundaryDir].low && lhs[mergeDir].low < rhs[mergeDir].low);
    });
    std::vector<BoxT> mergedBoxes;
    mergedBoxes.push_back(boxes.front());
    for (int i = 1; i < boxes.size(); ++i) {
        auto& lastBox = mergedBoxes.back();
        auto& slicedBox = boxes[i];
        if (slicedBox[boundaryDir] == lastBox[boundaryDir] &&
            slicedBox[mergeDir].low <= lastBox[mergeDir].high) {  // aligned and intersected
            lastBox[mergeDir] = lastBox[mergeDir].UnionWith(slicedBox[mergeDir]);
        } else {  // neither misaligned not seperated
            mergedBoxes.push_back(slicedBox);
        }
    }
    boxes = move(mergedBoxes);
}

// 扫描所有矩形边界切成条带，再合并/拼接覆盖相同区域的矩形。
// Slice polygons along sliceDir
// sliceDir: 0 for x/vertical, 1 for y/horizontal
// assume no degenerated case
template <typename T>
void SlicePolygons(std::vector<BoxT<T>>& boxes, int sliceDir) {
    // sliceDir 切片轴，sweepDir 扫描轴，locs 所有扫描轴边界排序去重。
    // slicedBoxes 新片列表，box 原矩形，slicedBox 当前条带，itLoc/itEnd 当前/尾后边界迭代器。
    // Line sweep in sweepDir = 1 - sliceDir
    // Suppose sliceDir = y and sweepDir = x (sweep from left to right)
    // Not scalable impl (brute force interval query) but fast for small case
    if (boxes.size() <= 1) return;

    // sort slice lines in sweepDir
    int sweepDir = 1 - sliceDir;
    std::vector<T> locs;
    for (const auto& box : boxes) {
        locs.push_back(box[sweepDir].low);
        locs.push_back(box[sweepDir].high);
    }
    std::sort(locs.begin(), locs.end());
    locs.erase(std::unique(locs.begin(), locs.end()), locs.end());

    // slice each box
    std::vector<BoxT<T>> slicedBoxes;
    for (const auto& box : boxes) {
        BoxT<T> slicedBox = box;
        auto itLoc = std::lower_bound(locs.begin(), locs.end(), box[sweepDir].low);
        auto itEnd = std::upper_bound(itLoc, locs.end(), box[sweepDir].high);
        while ((itLoc + 1) != itEnd) {
            slicedBox[sweepDir].Set(*itLoc, *(itLoc + 1));
            slicedBoxes.push_back(slicedBox);
            ++itLoc;
        }
    }
    boxes = move(slicedBoxes);

    // merge overlapped boxes along slice dir
    MergeRects(boxes, sliceDir);

    // stitch boxes along sweep dir
    MergeRects(boxes, sweepDir);
}

template <typename T>
class SegmentT : public BoxT<T> {
public:
    using BoxT<T>::BoxT;
    T length() const { return BoxT<T>::hp(); }
    bool IsRectilinear() const { return BoxT<T>::x() == 0 || BoxT<T>::y() == 0; }
};

}  // namespace utils
