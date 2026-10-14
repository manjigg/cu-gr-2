// 【零基础阅读】本表将并行长度、宽度档和间距要求组织成可查询的数据。
// vector 是可增长列表；同一位置的宽度与间距项应按表的组织规则对应。
// 这里仅定义字段及默认构造，没有计算路线或自动扫描整份工艺文件。
// 文件功能：保存并行长度、线宽与对应间距表数据。

/* 
 * File:   PhysicalSpacingTableData.h
 * Author: 
 *
 * Created on 12/2/2018, 17:35
 */

#ifndef PHYSICALDESIGN_PHYSICALSPACINGTABLEDATA_H
#define PHYSICALDESIGN_PHYSICALSPACINGTABLEDATA_H

namespace Rsyn {
// clsParallelLength 并行长度条件 DBU，clsParallelWidth[w] 线宽档，clsParallelWidthSpacing[w] 对应间隙。
// w 是规则档序号，两个列表以相同下标对应，不是 x/y 空间坐标。

class PhysicalSpacingTableData {
public:
	DBU clsParallelLength = 0;
	std::vector<DBU> clsParallelWidth;
	std::vector<DBU> clsParallelWidthSpacing;
	PhysicalSpacingTableData() = default;
}; // end class 

} // end namespace 

#endif /* PHYSICALDESIGN_PHYSICALSPACINGTABLEDATA_H */
