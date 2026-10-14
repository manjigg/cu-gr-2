// 文件功能：将 Net 的物理引脚矩形映射到 GCell，生成去重后的候选接入点及线网包围盒。
// 接入点为 (层索引,x网格索引,y网格索引)，不代表详细布线的精确轨道坐标。
//
// 【白话流程】逐引脚取物理矩形 -> 查覆盖的格子 -> 给格子附上层编号 -> 去重。
// 例如矩形覆盖 M2 上 (1,1)、(1,2)、(2,1)、(2,2)，这些都能成为候选。
// 同一个引脚有多个重叠矩形时，格子可能重复出现，所以 included 像签到名单。
// 所有候选生成后，再求一个大矩形 boundingBox，表示该线网大约分布在什么范围。
// 这里没有选哪个候选最好；这个决策在 GridGraph::selectAccessPoints。
#include "GRNet.h"


GRNet::GRNet(const Net& baseNet, const Design& design, const GridGraph& gridGraph) {
    // baseNet 输入连接名单；design 提供物理形状；gridGraph 提供格子边界和位置编码。
    // pinRefs 所有端点引用，numPins 原端点数，pinIndex 当前端点在外层列表的下标。
    // pinShapes 当前引脚所有全局 DBU 矩形；pinShape 当前矩形；cells 相交格子下标范围。
    // x/y 当前格子下标，point 附真实层后的三维格子位置，hash 位置编码；included 去重集合。
    // accessPoints 单引脚候选列表；point 在末循环为当前候选，供扩大 boundingBox。
    index = baseNet.getIndex();
    name = baseNet.getName();
    const auto& pinRefs = baseNet.getAllPinRefs();
    int numPins = pinRefs.size();
    // 先给每个引脚准备一个空列表：外层下标必须与原 pinRefs 的引脚顺序对应。
    pinAccessPoints.resize(numPins);
    for (int pinIndex = 0; pinIndex < pinRefs.size(); pinIndex++) {
        vector<BoxOnLayer> pinShapes;
        design.getPinShapes(pinRefs[pinIndex], pinShapes);
        // 同一引脚可能由多个重叠矩形组成，三维哈希避免重复加入同一个网格点。
        robin_hood::unordered_set<uint64_t> included;
        for (const auto& pinShape : pinShapes) {
            // 输入是 DBU 矩形，输出是格子索引矩形；这一步改变坐标含义。
            utils::BoxT<int> cells = gridGraph.rangeSearchCells(pinShape);
            for (int x = cells.x.low; x <= cells.x.high; x++) {
                for (int y = cells.y.low; y <= cells.y.high; y++) {
                    GRPoint point(pinShape.layerIdx, x, y);
                    // 把三个整数位置编码为一个整数钥匙，用集合快速判断是否出现过。
                    uint64_t hash = gridGraph.hashCell(point);
                    if (included.find(hash) == included.end()) {
                        pinAccessPoints[pinIndex].emplace_back(pinShape.layerIdx, x, y);
                        included.insert(hash);
                    }
                }
            }
        }
    }
    for (const auto& accessPoints : pinAccessPoints) {
        for (const auto& point : accessPoints) {
            // Update 不丢掉旧范围，而是必要时扩大上下界，直到包含所有候选点。
            boundingBox.Update(point);
        }
    }
}

// void GRNet::getGuides(vector<std::pair<int, utils::BoxT<int>>>& guides) const {
//     if (!routingTree) return;
//     GRTreeNode::preorder(routingTree, [&](std::shared_ptr<GRTreeNode> node) {
//         for (const auto& child : node->children) {
//             if (node->layerIdx == child->layerIdx) {
//                 guides.emplace_back(
//                     node->layerIdx, utils::BoxT<int>(
//                         min(node->x, child->x), min(node->y, child->y),
//                         max(node->x, child->x), max(node->y, child->y)
//                     )
//                 );
//             } else {
//                 int maxLayerIndex = max(node->layerIdx, child->layerIdx);
//                 for (int layerIdx = min(node->layerIdx, child->layerIdx); layerIdx <= maxLayerIndex; layerIdx++) {
//                     guides.emplace_back(layerIdx, utils::BoxT<int>(node->x, node->y));
//                 }
//             }
//         }
//     });
// }
