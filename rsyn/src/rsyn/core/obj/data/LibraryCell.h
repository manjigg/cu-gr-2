// 文件功能：保存库单元的名称、引脚、弧和相关属性，供实例复用。
// 【零基础阅读】库单元是零件型号的逻辑定义，保存它有哪些引脚与内部弧。
// 与物理库单元不同，本层重点是逻辑关系；尺寸/引脚矩形在 phy 模块保存。
// 列表下标可定位引脚/弧，名称索引便于由 "A"/"Y" 找到对应模板引脚。
// 此处定义存储格式，实际创建模板和建立关系由设计导入流程完成。
/* Copyright 2014-2018 Rsyn
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
 
namespace Rsyn {
// 【字段】name 型号名，pins/arcs 模板端点/弧列表，size.x/y DBU 宽高，tag 型号标签。
// numInputPins/numOutputPins/numInOutPins 是输入/输出/双向引脚数量，而非某个引脚编号。

struct LibraryCellTagData {
	LogicTypeTag logicType : 2;
	BufferTypeTag bufferType : 2;
	TieTypeTag tieType : 2;

	LibraryCellTagData() :
		logicType(LOGIC_TYPE_TAG_NOT_SPECIFIED),
		bufferType(BUFFER_TYPE_TAG_NOT_SPECIFIED),
		tieType(TIE_TYPE_TAG_NOT_SPECIFIED) {
	} // end constructor
}; // end struct

// -----------------------------------------------------------------------------

struct LibraryCellData : ObjectData {
	Design design;
	std::string name;
	std::vector<LibraryPin> pins;
	std::vector<LibraryArc> arcs;
	DBUxy size;

	// User flags.
	LibraryCellTagData tag;

	// TODO: use array
	int numInputPins;
	int numOutputPins;
	int numInOutPins;

	LibraryCellData() : 
		design(nullptr),
		numInputPins(0),
		numOutputPins(0), 
		numInOutPins(0),
		size(0, 0) {
	} // constructor
}; // end struct

} // end namespace
