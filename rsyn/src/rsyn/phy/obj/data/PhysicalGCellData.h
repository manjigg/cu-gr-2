// 文件功能：保存 GCELLGRID 的方向、起始位置、数量和步长。
// 【零基础阅读】一条 GCELLGRID 记录用“起点+数量+步长”定义一组重复网格边界。
// 起点 0、步长 100、数量 4 可表达 0/100/200/300，间隔形成格子。
// 方向说明这组边界怎样排列；CUGR 将这些记录展开成 x/y 边界列表。
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
 
#ifndef PHYSICALGCELLDATA_H
#define PHYSICALGCELLDATA_H

namespace Rsyn {
// clsDirection 网格线方向，clsLocation 首线 DBU 位置，clsNumTracks 重复数，clsStep DBU 步长。
// 第 i 条位置=clsLocation+i*clsStep，i 为序号；此处无实时 capacity/demand。

class PhysicalGCellData {
	friend class PhysicalDesign;
public:
	//! @brief Direction of the GCell
	//! HORIZONTAL is equal to Y in DEF and VERTICAL is equal to X in DEF
	PhysicalGCellDirection clsDirection;
	//! @brief Each GCELLGRID statement specifies the location of the first vertical (x) and first horizontal (y) track. Source: DEF Manual.
	DBU clsLocation = 0;
	//! @brief Specifies the number of columns in the grid if Direction is VERTICAL
	//! Specifies the number of rows in the grid if Direction is HORIZONTAL
	int clsNumTracks = 0;
	//! @brief Specifies the spacing between tracks.
	DBU clsStep = 0;
	//! @brief	default constructor
	PhysicalGCellData() = default;
}; // end class 
} // end namespace 

#endif /* PHYSICALGCELLDATA_H */
