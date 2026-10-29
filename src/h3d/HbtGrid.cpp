#include "HbtGrid.h"
#include <algorithm>
#include <limits>
#include <queue>
#include <set>
#include <stdexcept>

namespace h3d {
namespace {
DBU distance(const utils::BoxT<DBU>& a, const utils::BoxT<DBU>& b) {
    return utils::Dist(a, b);
}
}

HbtGrid::HbtGrid(std::vector<DBU> x, std::vector<DBU> y, const std::vector<int>& blocked,
                 bool allowCoincidentRepresentatives):
    xs(std::move(x)), ys(std::move(y)) {
    for (const auto* axis : {&xs, &ys}) {
        if (axis->empty() || !std::is_sorted(axis->begin(), axis->end()) ||
            (!allowCoincidentRepresentatives && std::adjacent_find(axis->begin(), axis->end()) != axis->end())) {
            throw std::invalid_argument("Site grid axes must be nonempty and strictly increasing");
        }
    }
    if (xs.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) / ys.size()) {
        throw std::length_error("Site grid is too large");
    }
    owners.assign(xs.size() * ys.size(), -1);
    leaves.resize(owners.size());
    for (int site : blocked) owners.at(site) = -2;
    build(0, xs.size() - 1, 0, ys.size() - 1, -1);
}

utils::PointT<DBU> HbtGrid::point(int site) const {
    if (site < 0 || site >= size()) throw std::out_of_range("Invalid site index");
    return {xs[site % xs.size()], ys[site / xs.size()]};
}

int HbtGrid::siteAt(utils::PointT<DBU> p) const {
    auto x = std::lower_bound(xs.begin(), xs.end(), p.x);
    auto y = std::lower_bound(ys.begin(), ys.end(), p.y);
    if (x == xs.end() || y == ys.end() || *x != p.x || *y != p.y) return -1;
    return (y - ys.begin()) * xs.size() + x - xs.begin();
}

int HbtGrid::availableSiteAt(utils::PointT<DBU> p) const {
    auto xr = std::equal_range(xs.begin(), xs.end(), p.x);
    auto yr = std::equal_range(ys.begin(), ys.end(), p.y);
    for (auto y = yr.first; y != yr.second; ++y) for (auto x = xr.first; x != xr.second; ++x) {
        int site = (y - ys.begin()) * xs.size() + x - xs.begin();
        if (owners[site] == -1) return site;
    }
    return -1;
}

int HbtGrid::build(int xl, int xh, int yl, int yh, int parent) {
    int index = quads.size();
    Quad quad;
    quad.xl = xl; quad.xh = xh; quad.yl = yl; quad.yh = yh; quad.parent = parent;
    quad.bounds = {xs[xl], ys[yl], xs[xh], ys[yh]};
    quads.push_back(quad);
    if ((xh - xl + 1) * static_cast<std::int64_t>(yh - yl + 1) <= 16) {
        for (int y = yl; y <= yh; y++) for (int x = xl; x <= xh; x++) {
            int site = y * xs.size() + x;
            leaves[site] = index;
            if (owners[site] == -1) quads[index].available++;
        }
    } else {
        int xm = xl + (xh - xl) / 2, ym = yl + (yh - yl) / 2;
        int slot = 0;
        for (auto yr : {std::make_pair(yl, ym), std::make_pair(ym + 1, yh)}) {
            for (auto xr : {std::make_pair(xl, xm), std::make_pair(xm + 1, xh)}) {
                if (xr.first > xr.second || yr.first > yr.second) continue;
                int child = build(xr.first, xr.second, yr.first, yr.second, index);
                quads[index].children[slot++] = child;
                quads[index].available += quads[child].available;
            }
        }
    }
    return index;
}

std::vector<int> HbtGrid::within(const utils::BoxT<DBU>& box) const {
    std::vector<int> result, stack = {0};
    while (!stack.empty()) {
        int index = stack.back(); stack.pop_back();
        const auto& quad = quads[index];
        if (!quad.available || !quad.bounds.HasIntersectWith(box)) continue;
        if (quad.children[0] != -1) {
            for (int child : quad.children) if (child != -1) stack.push_back(child);
        } else {
            for (int y = quad.yl; y <= quad.yh; y++) for (int x = quad.xl; x <= quad.xh; x++) {
                int site = y * xs.size() + x;
                if (owners[site] == -1 && xs[x] >= box.lx() && xs[x] <= box.hx() &&
                    ys[y] >= box.ly() && ys[y] <= box.hy()) result.push_back(site);
            }
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<int> HbtGrid::nearestAll(const utils::BoxT<DBU>& box, int dx, int dy) const {
    if (dx < -1 || dx > 1 || dy < -1 || dy > 1 || (!dx && !dy)) {
        throw std::invalid_argument("A directional query needs one of the eight directions");
    }
    auto region = bounds();
    for (int axis = 0; axis < 2; axis++) {
        int d = axis == 0 ? dx : dy;
        if (d < 0) {
            if (box[axis].low <= region[axis].low) return {};
            region[axis].high = std::min(region[axis].high, box[axis].low - 1);
        } else if (d > 0) {
            if (box[axis].high >= region[axis].high) return {};
            region[axis].low = std::max(region[axis].low, box[axis].high + 1);
        } else {
            region[axis] = region[axis].IntersectWith(box[axis]);
        }
    }
    if (!region.IsValid()) return {};
    using Entry = std::pair<DBU, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> queue;
    queue.emplace(distance(box, region), 0);
    DBU bestDistance = std::numeric_limits<DBU>::max();
    std::vector<int> best;
    while (!queue.empty()) {
        auto entry = queue.top(); queue.pop();
        if (entry.first > bestDistance) break;
        const auto& quad = quads[entry.second];
        if (!quad.available || !quad.bounds.HasIntersectWith(region)) continue;
        if (quad.children[0] != -1) {
            for (int child : quad.children) if (child != -1) {
                const auto& q = quads[child];
                if (q.available && q.bounds.HasIntersectWith(region)) {
                    queue.emplace(distance(box, q.bounds.IntersectWith(region)), child);
                }
            }
        } else {
            for (int y = quad.yl; y <= quad.yh; y++) for (int x = quad.xl; x <= quad.xh; x++) {
                int site = y * xs.size() + x;
                if (owners[site] != -1 || xs[x] < region.lx() || xs[x] > region.hx() ||
                    ys[y] < region.ly() || ys[y] > region.hy()) continue;
                DBU cost = utils::Dist(box, point(site));
                if (cost < bestDistance) { best.clear(); bestDistance = cost; }
                if (cost == bestDistance) best.push_back(site);
            }
        }
    }
    std::sort(best.begin(), best.end());
    return best;
}

int HbtGrid::nearest(const utils::BoxT<DBU>& box, int dx, int dy) const {
    auto all = nearestAll(box, dx, dy);
    return all.empty() ? -1 : all.front();
}

std::vector<int> HbtGrid::candidates(const utils::BoxT<DBU>& box) const {
    auto result = within(box);
    for (int dx = -1; dx <= 1; dx++) for (int dy = -1; dy <= 1; dy++) {
        if (!dx && !dy) continue;
        int site = nearest(box, dx, dy);
        if (site != -1) result.push_back(site);
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

void HbtGrid::change(int site, int delta) {
    for (int quad = leaves[site]; quad != -1; quad = quads[quad].parent) quads[quad].available += delta;
}

void HbtGrid::occupy(const std::vector<int>& sites, int net) {
    if (net < 0) throw std::invalid_argument("Invalid HBT owner");
    std::set<int> unique(sites.begin(), sites.end());
    for (int site : unique) if (owners.at(site) != -1 && owners[site] != net) {
        throw std::runtime_error("HBT site is blocked or occupied by another net");
    }
    for (int site : unique) if (owners[site] == -1) { owners[site] = net; change(site, -1); }
}

void HbtGrid::release(const std::vector<int>& sites, int net) {
    if (net < 0) throw std::invalid_argument("Invalid HBT owner");
    std::set<int> unique(sites.begin(), sites.end());
    for (int site : unique) if (owners.at(site) != net) throw std::runtime_error("HBT owner mismatch");
    for (int site : unique) { owners[site] = -1; change(site, 1); }
}

} // namespace h3d
