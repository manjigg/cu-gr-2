// 文件功能：从 Rsyn/LEF 提取金属层宽度、轨距、最小面积和间距规则，并实现轨道范围查询。
// LEF 微米值在读取时换算为 DBU；这些规则用于全局布线的资源与过孔代价估计。
//
// 【白话流程】从 Rsyn 层拿到 LEF 原始对象，复制名称/方向/宽度，再读首选轨道。
// round 是四舍五入：LEF 的小数微米乘每微米 DBU 数后，转成整数坐标。
// static_cast<DBU> 明确告诉编译器“把结果转成 DBU 类型”。
// getTrackLocation 用首轨+编号×轨距计算位置；rangeSearchTracks 做反向计算。
// 例：首轨 10、轨距 20、查询 [25,65]，包含的轨道在 30/50，对应索引 [1,2]。
// 查询时 ceil 向上取整，floor 向下取整，所以不会把范围外轨道误计入。
// includeBound=false 时再排除正好落在区间端点的轨道。
#include "Layers.h"

MetalLayer::MetalLayer(const Rsyn::PhysicalLayer& rsynLayer, const vector<Rsyn::PhysicalTracks>& rsynTracks, const DBU libDBU) {
    // rsynLayer 当前层代理，layer 底层 LEF 层地址，rsynTracks 为可选轨道组，rsynTrack 当前组。
    // libDBU 换算比例。先复制名称/方向/宽度，再只选首选方向轨道组，填首轨/轨距/轨数。
    // numSpacingTable 表数量，tableIdx 当前表下标，parallel 当前并行间距表；
    // numLength/numWidth 为长度/宽度档数量，l/w 为档下标，不是物理坐标。
    // numSpace 线端相关条目数，spacingIdx 条目下标；spacing 间隙，eolWidth 端宽，eolWithin 范围。
    // LEF 小数先乘 DBU 比例并 round；面积乘 libDBU²，不能只乘一次。
    lefiLayer* layer = rsynLayer.getLayer();
    name = layer->name();
    index = rsynLayer.getRelativeIndex();
    direction = strcmp(layer->direction(), "HORIZONTAL") ? V : H;
    width = static_cast<DBU>(std::round(layer->width() * libDBU));
    minWidth = static_cast<DBU>(std::round(layer->minwidth() * libDBU));
    
    for (const Rsyn::PhysicalTracks& rsynTrack : rsynTracks) {
        if ((rsynTrack.getDirection() == Rsyn::TRACK_HORIZONTAL) == (direction == H)) {
            // Only consider the tracks of the prefered direction
            pitch = rsynTrack.getSpace();
            numTracks = rsynTrack.getNumberOfTracks();
            firstTrackLoc = rsynTrack.getLocation();
            lastTrackLoc = firstTrackLoc + pitch * (numTracks - 1);
            break;
        }
    }
    // Design rules not parsed thoroughly
    if (pitch <= 0 || numTracks <= 0 || width <= 0) {
        throw std::invalid_argument("Missing preferred-direction tracks or invalid width for layer " + name);
    }
    // Min area
    minArea = static_cast<DBU>(std::round(layer->area() * libDBU * libDBU));
    // 将最小面积折算成过孔附近需要补充的导线长度，扣除一个线宽的局部面积。
    minLength = max(minArea / width - width, (DBU)0);
    
    // Parallel run spacing
    const int numSpacingTable = layer->numSpacingTable();
    if (numSpacingTable == 0) {
        log() << "Warning in " << __func__ << ": no spacing table for " << name << std::endl;
    } else {
        for (unsigned tableIdx = 0; tableIdx < numSpacingTable; tableIdx++) {
            if (!layer->spacingTable(tableIdx)->isParallel()) continue;
            const lefiParallel* parallel = layer->spacingTable(tableIdx)->parallel();
            const int numLength = parallel->numLength();
            if (numLength > 0) {
                parallelLength.resize(numLength);
                for (unsigned l = 0; l < numLength; l++) {
                    parallelLength[l] = static_cast<DBU>(std::round(parallel->length(l) * libDBU));
                }
            }
            const int numWidth = parallel->numWidth();
            if (numWidth > 0) {
                parallelWidth.resize(numWidth);
                parallelSpacing.resize(numWidth);
                for (unsigned w = 0; w < numWidth; w++) {
                    parallelWidth[w] = static_cast<DBU>(std::round(parallel->width(w) * libDBU));
                    parallelSpacing[w].resize(max(1, numLength), 0);
                    for (int l = 0; l < numLength; l++) {
                        parallelSpacing[w][l] = static_cast<DBU>(std::round(parallel->widthSpacing(w, l) * libDBU));
                    }
                }
            }
        }
    }
    defaultSpacing = getParallelSpacing(width);
    
    // End-of-line spacing
    if (!layer->hasSpacingNumber()) {
        log() << "Warning in " << __func__ << ": no spacing rules for " << name << std::endl;
    } else {
        const int numSpace = layer->numSpacing();
        for (int spacingIdx = 0; spacingIdx < numSpace; ++spacingIdx) {
            const DBU spacing = static_cast<DBU>(std::round(layer->spacing(spacingIdx) * libDBU));
            const DBU eolWidth = static_cast<DBU>(std::round(layer->spacingEolWidth(spacingIdx) * libDBU));
            const DBU eolWithin = static_cast<DBU>(std::round(layer->spacingEolWithin(spacingIdx) * libDBU));
    
            // Not considered
            // const DBU parSpace = static_cast<DBU>(std::round(layer->spacingParSpace(spacingIdx) * libDBU));
            // const DBU parWithin = static_cast<DBU>(std::round(layer->spacingParWithin(spacingIdx) * libDBU));
    
            // Pessimistic
            maxEolSpacing = std::max(maxEolSpacing, spacing);
            maxEolWidth = std::max(maxEolWidth, eolWidth);
            maxEolWithin = std::max(maxEolWithin, eolWithin);
        }
    }
    
    // 已复制当前算法需要的属性，原实现在这里释放底层 LEF 对象。
    delete rsynLayer.getLayer();
}

DBU MetalLayer::getTrackLocation(const int trackIndex) const {
    return firstTrackLoc + trackIndex * pitch;
}

// 将物理坐标范围映射为轨道索引闭区间；ceil/floor 保证轨道落在范围内。
utils::IntervalT<int> MetalLayer::rangeSearchTracks(const utils::IntervalT<DBU>& locRange, bool includeBound) const {
    // locRange 输入物理区间；trackRange 输出轨道编号区间；includeBound 是否计端点上的轨道。
    // 最左合法编号向上取整，最右合法编号向下取整，并截到首/末轨道范围。
    utils::IntervalT<int> trackRange(
        ceil(double(max(locRange.low, firstTrackLoc) - firstTrackLoc) / double(pitch)),
        floor(double(min(locRange.high, lastTrackLoc) - firstTrackLoc) / double(pitch))
    );
    if (!trackRange.IsValid()) return trackRange;
    if (!includeBound) {
        if (getTrackLocation(trackRange.low) == locRange.low) ++trackRange.low;
        if (getTrackLocation(trackRange.high) == locRange.high) --trackRange.high;
    }
    return trackRange;
}

DBU MetalLayer::getParallelSpacing(const DBU width, const DBU length) const {
    // width 查询线宽 DBU，length 查询并行长度 DBU；w/l 是命中的两个规则档下标。
    // 返回 parallelSpacing[w][l] 间距 DBU；length=0 时直接取长度档 0。
    // 间距表有若干宽度/长度档；从末档往前找，本实现使用下面的 >= 比较规则。
    // length=0 是特殊情况：直接取选定宽度档的第 0 个长度档。
    unsigned w = parallelWidth.size() - 1;
    while (w > 0 && parallelWidth[w] >= width) w--;
    if (length == 0) return parallelSpacing[w][0];
    unsigned l = parallelLength.size() - 1;
    while (l > 0 && parallelLength[l] >= length) l--;
    return parallelSpacing[w][l];
}
