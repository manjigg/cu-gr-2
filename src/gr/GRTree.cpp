#include "GRTree.h"

void GRTreeNode::preorder(std::shared_ptr<GRTreeNode> node, std::function<void(std::shared_ptr<GRTreeNode>)> visit) {
    visit(node);
    for (auto& child : node->children) preorder(child, visit);
}

void GRTreeNode::print(std::shared_ptr<GRTreeNode> node) {
    preorder(node, [](std::shared_ptr<GRTreeNode> node) {
        log() << *node << (node->children.size() > 0 ? " -> " : "");
        for (auto& child : node->children) std::cout << *child << (child == node->children.back() ? "" : ", ");
        std::cout << std::endl;
    });
}
void GRTreeNode::attach(std::shared_ptr<GRTreeNode> child, GREdgeType type, int site) {
    if (!child || ((type == GREdgeType::HBT) != (dieIdx != child->dieIdx)) ||
        ((type == GREdgeType::HBT) != (site >= 0)))
        throw std::invalid_argument("Invalid routing-tree edge");
    if (type == GREdgeType::WIRE && (layerIdx != child->layerIdx || (x!=child->x && y!=child->y)))
        throw std::invalid_argument("Non-rectilinear wire");
    if (type == GREdgeType::VIA && (layerIdx == child->layerIdx || x!=child->x || y!=child->y))
        throw std::invalid_argument("Invalid via stack");
    child->incoming=type;
    child->siteId=site;
    children.push_back(std::move(child));
}
