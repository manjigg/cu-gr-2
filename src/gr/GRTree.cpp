// 文件功能：实现布线树的递归前序遍历和调试打印，供资源提交、统计及 guide 生成复用。
// preorder 假定传入有效根节点，先访问当前节点，再依次递归访问子树。
//
// 【白话职责】本文件提供“把树上的每个点都看一遍”的通用方式。
// preorder(node,visit) 先让 visit 处理当前点，再对所有孩子重复同样动作。
// 重复调用自己叫递归；走到没有孩子的点时 for 循环为空，自然返回上一层。
// visit 是调用方交进来的函数：可用于统计、加减需求、生成 guide 等不同工作。
// 比如树 A 有孩子 B/C，B 有孩子 D，前序访问顺序为 A、B、D、C。
// print 使用这个遍历逐点打印父子关系，方便检查结果，不会重新计算路线。
#include "GRTree.h"

void GRTreeNode::preorder(std::shared_ptr<GRTreeNode> node, std::function<void(std::shared_ptr<GRTreeNode>)> visit) {
    // node 当前点，visit 调用者给的逐点操作，child 当前孩子；先 visit(node) 再遍历各 child。
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
