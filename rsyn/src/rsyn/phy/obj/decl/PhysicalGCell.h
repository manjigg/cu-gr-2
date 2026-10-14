// 文件功能：声明 GCell 方向、位置、重复数量和步长读取接口。
// 【零基础阅读】查询一组重复网格线的起点、数量、步长和方向。
// getNumTracks 在此对应 GCELLGRID 重复数量，不要混成金属 TRACKS 总轨数。
// 调用方展开这些定义后得到实际网格边界，本代理只返回现有参数。
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
 
#ifndef PHYSICALGCELL_H
#define PHYSICALGCELL_H

namespace Rsyn {
// clsDirection 线方向，clsLocation 首线位置 DBU，clsNumTracks 重复数量，clsStep 步长 DBU。

class PhysicalGCell : public Proxy<PhysicalGCellData> {
	friend class PhysicalDesign;
protected:
	PhysicalGCell(PhysicalGCellData * data) : Proxy(data) {}
public:
	//! @brief	Default constructor. 
	//!		PhysicalGCellData pointer is initialized to nullptr.		
	PhysicalGCell()  {}
	//! @brief 
	PhysicalGCell(std::nullptr_t)  {}
	
	//! @brief Returns GCell direction
	Rsyn::PhysicalGCellDirection getDirection() const;
	
	//! @brief If direction is vertical, then numTracks means the number of columns
	//! If direction is Horizontal, then numTracks means the number of rows
	int getNumTracks() const;
	//! @brief If direction is Horizontal, then location is Y value.
	//! If direction is vertical, then location is X value.
	DBU getLocation() const;

	//! @brief Returns the spacing between GCell tracks. 
	DBU getStep() const;
}; // end class 
} // end namespace 

#endif /* PHYSICALGCELL_H */
