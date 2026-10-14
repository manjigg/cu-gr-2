// 文件功能：声明过孔几何的矩形边界和所属层接口。
// 【零基础阅读】单个过孔形状由边界和工艺层组成，整只过孔可以有多个这样的形状。
// 取边界后加路由点位移，才得到芯片坐标上的占用矩形。
// 这是形状读取接口，不在这里判断整条线网是否连通。
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
 * File:   PhysicalViaGeometry.h
 * Author: jucemar
 *
 * Created on November 12, 2018, 5:52 PM
 */

#ifndef RSYN_DATABASE_PHYSICALVIAGEOMETRY_H
#define RSYN_DATABASE_PHYSICALVIAGEOMETRY_H

namespace Rsyn {
// clsBounds 过孔局部 DBU 矩形，clsMaskNumber 光罩编号，必须结合实际过孔放置点才得全局坐标。

class PhysicalViaGeometry : public Proxy<ViaGeometryData> {
	friend class PhysicalDesign;
	RSYN_FRIEND_OF_GENERIC_LIST_COLLECTION;
protected:
	//! @brief Constructs a Rsyn::PhysicalViaGeometry object with a pointer to Rsyn::ViaGeometryData.

	PhysicalViaGeometry(ViaGeometryData * data) : Proxy(data) {
	}
public:
	//! @brief Constructs a Rsyn::PhysicalViaGeometry object with a null pointer to Rsyn::ViaGeometryData.

	PhysicalViaGeometry() : Proxy(nullptr) {
	}
	//! @brief Constructs a Rsyn::PhysicalViaGeometry object with a null pointer to Rsyn::ViaGeometryData.

	PhysicalViaGeometry(std::nullptr_t) : Proxy(nullptr) {
	}
	
	const Bounds & getBounds() const;
	int getMaskNumber() const;
	
}; // end class

} // end namespace

#endif /* RSYN_DATABASE_PHYSICALVIAGEOMETRY_H */
