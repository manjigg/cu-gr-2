// 文件功能：定义信号线网 Net 及 PinReference，记录每个线网要连接的实例/引脚索引。
// 此层仅表达连接关系；引脚物理形状由 Design 查询，网格接入点由 GRNet 生成。
//
// 【白话职责】Net 是“必须接通的一组接线端”的名单，不是已经画好的导线。
// 例如线网 N1 需要连 U1 的 A 引脚和 U2 的 B 引脚，这里保存它们的编号。
// PinReference 不复制引脚矩形，只保存 instanceIndex/pinIndex 两个定位数字。
// 类比通讯录：号码指出去哪里找人，不等于把这个人的全部资料复制一份。
// addPinRef 只把一项加入名单；实际读取几何与选路发生在其他模块。
#pragma once
#include "global.h"
#include "GeoTypes.h"

// pinIndex=-1 表示外部端口；其余索引指向实例所属 Macro 的信号引脚。
struct PinReference {
    // instanceIndex 是 Design.instances 下标，pinIndex 是其模板信号引脚下标。
    // PORT 用 -1 作分类标记，不应拿它直接对引脚列表作 [-1] 访问。
    int instanceIndex;
    // 假设 instances[3] 是 U1，则 instanceIndex=3；索引从 0 起算。
    int pinIndex; // pin index inside the instance
    
    PinReference(int instIdx, int pIdx): instanceIndex(instIdx), pinIndex(pIdx) {}
};

class Net {
public:
    Net(int idx, std::string _name): index(idx), name(_name) {};
    void addPinRef(int instanceIndex, int pinIndex) {
        // emplace_back 使用这两个编号在 vector 末尾构造 PinReference。
        // 此处没有寻找路径，只是在记录“以后必须连接谁”。
        pinRefs.emplace_back(instanceIndex, pinIndex);
    }
    
    int getIndex() const { return index; }
    std::string getName() const { return name; }
    const vector<PinReference>& getAllPinRefs() const { return pinRefs; }
    // const 引用让调用者查看名单而不复制/修改；真实列表由 Net 自己持有。
    
private:
    // index 普通信号网编号，name 输入网名，pinRefs[i] 第 i 个必须连接的端点。
    int index;
    std::string name;
    vector<PinReference> pinRefs;
};
