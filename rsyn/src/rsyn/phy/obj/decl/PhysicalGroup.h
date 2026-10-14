// 文件功能：声明物理分组名称、成员和区域关联接口。
// 【零基础阅读】此菜单用于查询分组名称、成员或区域关联。
// 组是逻辑上的成员集合，约束区域是几何范围，不是把成员位置自动叠在一起。
// 具体访问步骤在 impl，字段在 data；先理解返回对象种类再追实现。
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
 * File:   PhysicalGroup.h
 * Author: jucemar
 *
 * Created on 09 de Abril de 2017, 16:45
 */

#ifndef PHYSICALDESIGN_PHYSICALGROUP_H
#define PHYSICALDESIGN_PHYSICALGROUP_H

namespace Rsyn {
// clsPatterns 成员文字匹配模式，clsRegion 关联区域代理；成员模式和实际对象列表不是一回事。

class PhysicalGroup : public Proxy<PhysicalGroupData> {
	friend class PhysicalDesign;
	friend class PhysicalDesignData;
	RSYN_FRIEND_OF_GENERIC_LIST_COLLECTION;
protected:
	//! @brief Constructs a Rsyn::PhysicalGroup object with a pointer to Rsyn::PhysicalGroupData.

	PhysicalGroup(PhysicalGroupData * data) : Proxy(data) {
	}
public:
	//! @brief Constructs a Rsyn::PhysicalGroup object with a null pointer to Rsyn::PhysicalGroupData.

	PhysicalGroup() : Proxy(nullptr) {
	}
	//! @brief Constructs a Rsyn::PhysicalGroup object with a null pointer to Rsyn::PhysicalGroupData.

	PhysicalGroup(std::nullptr_t) : Proxy(nullptr) {
	}

	//! @brief Returns region's name
	const std::string & getName() const;
	//! @brief Returns the PhysicalRegion object associated to PhysicalGroup.
	Rsyn::PhysicalRegion getPhysicalRegion() const;
	//! @brief Returns the constant reference to the Region patters vector.
	const std::vector<std::string> & allPatterns() const;

}; // end class 

} // end namespace 


#endif /* PHYSICALDESIGN_PHYSICALGROUP_H */
