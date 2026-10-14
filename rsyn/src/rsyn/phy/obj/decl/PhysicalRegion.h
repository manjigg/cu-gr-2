// 文件功能：声明物理区域名称、类型和边界约束接口。
// 【零基础阅读】查询物理约束区域的名字、类别和边界集合。
// 多个矩形可以拼成区域；一个大包围盒未必准确代表区域的所有可用位置。
// 保存和查询约束不等于自动执行约束，实际应用由后续算法负责。
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
 
/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   PhysicalRegion.h
 * Author: jucemar
 *
 * Created on 09 de Abril de 2017, 14:55
 */

#ifndef PHYSICALDESIGN_PHYSICALREGION_H
#define PHYSICALDESIGN_PHYSICALREGION_H

namespace Rsyn {
// clsBounds[i] 区域第 i 块 DBU 矩形；区域名是文字键，类型标志是约束类别而非信号方向。

class PhysicalRegion : public Proxy<PhysicalRegionData> {
	friend class PhysicalDesign;
	friend class PhysicalDesignData;
	RSYN_FRIEND_OF_GENERIC_LIST_COLLECTION;
protected:
	//! @brief Constructs a Rsyn::PhysicalRegion object with a pointer to Rsyn::PhysicalRegionData.

	PhysicalRegion(PhysicalRegionData * data) : Proxy(data) {
	}
public:
	//! @brief Constructs a Rsyn::PhysicalRegion object with a null pointer to Rsyn::PhysicalRegionData.

	PhysicalRegion() : Proxy(nullptr) {
	}
	//! @brief Constructs a Rsyn::PhysicalRegion object with a null pointer to Rsyn::PhysicalRegionData.

	PhysicalRegion(std::nullptr_t) : Proxy(nullptr) {
	}

	//! @brief Returns region's name
	const std::string & getName() const;
	//! @brief Returns region's type that may be FENCE or GUIDE.
	RegionType getType() const;
	//! @brief Returns the constant reference to the boundaries vector.
	const std::vector<Bounds> & allBounds() const;
	//! @brief Returns total number of boundaries. 
	std::size_t getNumBounds() const;

}; // end class 

} // end namespace 


#endif /* PHYSICALDESIGN_PHYSICALREGION_H */
