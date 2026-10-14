// 文件功能：声明放置行的位置、尺寸、朝向和 site 接口。
// 【零基础阅读】查询单元放置行的起点、大小、朝向和 site 模板。
// 放置行是摆单元的座位组织，不是布线金属轨道。
// 本接口读已有 DEF 布局资料，改变实例位置要走设计更新接口。
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
 * File:   PhysicalRow.h
 * Author: jucemar
 *
 * Created on 14 de Setembro de 2016, 22:29
 */

#ifndef PHYSICALDESIGN_PHYSICALROW_H
#define PHYSICALDESIGN_PHYSICALROW_H

namespace Rsyn {
// clsOrigin 行起点，clsStep 两轴步长均 DBU；clsNumSites[轴] 座位重复数，clsPhysicalSite 为尺寸模板。

class PhysicalRow : public Proxy<PhysicalRowData> {
	friend class PhysicalDesign;
	friend class PhysicalDesignData;
	RSYN_FRIEND_OF_GENERIC_LIST_COLLECTION;
protected:
	//! @brief Constructs a Rsyn::PhysicalRow object with a pointer to Rsyn::PhysicalRowData.
	PhysicalRow(PhysicalRowData * data) : Proxy(data) {}
public:
	//! @brief Constructs a Rsyn::PhysicalRow object with a null pointer to Rsyn::PhysicalRowData.
	PhysicalRow() : Proxy(nullptr) {}
	//! @brief Constructs a Rsyn::PhysicalRow object with a null pointer to Rsyn::PhysicalRowData.
	PhysicalRow(std::nullptr_t) : Proxy(nullptr) {}

	//! @brief Returns the Rsyn::PhysicalRow name. 
	std::string getName() const;
	//! @brief Returns the Rsyn::PhysicalRow site name. 
	std::string getSiteName() const;
	//! @brief Returns the Rsyn::PhysicalRow width in DBU. 
	DBU getWidth() const;
	//! @brief Returns the Rsyn::PhysicalRow height in DBU. 
	DBU getHeight() const ;
	//! @brief Returns the Rsyn::PhysicalRow lower-left X or Y point. 
	DBU getOrigin(const Dimension dim) const;
	//! @brief Returns the Rsyn::PhysicalRow lower-left point.
	DBUxy getOrigin() const;
	//! @brief Returns the Rsyn::PhysicalRow step length for X or Y.
	DBU getStep(const Dimension dim) const;
	//! @brief Returns the Rsyn::PhysicalRow step length. In X is the width while in Y is the height.
	DBUxy getStep() const;
	//! @brief Returns the total number of sites in the Rsyn::PhsyicalRow
	int getNumSites(const Dimension dim) const ;
	//! @brief Returns the Rsyn::PhysicalSite object.
	Rsyn::PhysicalSite getPhysicalSite() const;
	//! @brief Returns the site width in DBU.
	DBU getSiteWidth() const ;
	//! @brief Returns the site height in DBU.
	DBU getSiteHeight() const;
	//! @brief Returns the coordinate point in the left-lower (LOWER parameter) or right-upper (UPPER parameter) corner.
	DBUxy getCoordinate(const Boundary bound) const;
	//! @brief Returns the coordinate point in the left-lower (LOWER parameter) or right-upper (UPPER parameter) corner
	//! and for X or Y dimensions.
	DBU getCoordinate(const Boundary bound, const Dimension dim) const ;
	//! @brief Returns the rectangular Bounds of the Rsyn:PhsyicalRow.
	const Bounds &getBounds() const;
	//! @brief Returns the Rsyn::PhysicalOrientation of th eRsyn::PhysicalRow.
	//! @details Rsyn::PhysicalOrientation may be: 1) ORIENTATION_N, 2) ORIENTATION_E, 
	//! 3) ORIENTATION_S, 4) ORIENTATION_W, 5) ORIENTATION_FN, 6) ORIENTATION_FE,
	//! 7) ORIENTATION_FS, or 8) ORIENTATION_FW.
	Rsyn::PhysicalOrientation getSiteOrientation() const ;
	//! @brief Returns the Rsyn::PhysicalSymmetry of Rsyn::PhysicalRow.
	//! @details Rsyn::PhysicalSymmetry may be: 1) SYMMETRY_X, or 2) SYMMETRY_Y.
	Rsyn::PhysicalSymmetry getSymmetry() const;

}; // end class 

} // end namespace 


#endif /* PHYSICALDESIGN_PHYSICALROW_H */
