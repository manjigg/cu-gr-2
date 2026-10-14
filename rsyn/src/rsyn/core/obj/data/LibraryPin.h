// 文件功能：保存库引脚的名称、方向、用途及所属库单元信息。
// 【零基础阅读】库引脚像零件型号上的接线端说明，如名称 A、方向 INPUT、用途 SIGNAL。
// 实际引脚属于某个实例，库引脚属于型号，多个实例引脚可引用同一库定义。
// 方向和用途通常用枚举编码，名字使编号更易读；它们不是物理坐标。
// 本层存字段，几何形状请继续看 phy 的 PhysicalLibraryPin 系列。
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
// 【字段】lcell 所属型号，name 引脚名，direction 信号方向，pinUse 电源/信号用途。
// index 型号内引脚下标，与继承 id 的全局对象身份范围不同。

struct LibraryPinData : ObjectData {
	Design design;
	LibraryCell lcell;
	std::string name;
	Direction direction;
	// Mateus @ 20180917: Adding PinUse
	Use pinUse;
	int index;

	LibraryPinData() : 
		design(nullptr),
		lcell(nullptr),
		direction(UNKNOWN_DIRECTION),
		pinUse(UNKNOWN_USE),
		index(-1) {
	} // end constructor
}; // end struct

} // end namespace
