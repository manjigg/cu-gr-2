// 文件功能：声明工艺层类型、索引、宽度和底层 LEF 层查询接口。
// 【零基础阅读】本菜单查询层的身份、类型和规则，名字如 M2 是文字而索引是数字。
// getLayer 返回底层 LEF 层对象，CUGR 用其读取线宽、面积与间距表。
// 区分 ROUTING 金属层和 CUT 切孔层，不能把两种对象都算作可走导线的层。
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
 * File:   PhysicalLayer.h
 * Author: jucemar
 *
 * Created on 13 de Setembro de 2016, 19:12
 */

#ifndef PHYSICALDESIGN_PHYSICALLAYER_H
#define PHYSICALDESIGN_PHYSICALLAYER_H



namespace Rsyn {
// clsIndex 全层序号，clsRelativeIndex 同类别层序号，clsType 用途，clsWidth DBU 宽度。
// clsLower/clsUpper 层资料地址，getLayer 提供详细 LEF 层对象，非网格资源记录。

class PhysicalLayer : public Proxy<PhysicalLayerData> {
	friend class PhysicalDesign;
	friend class PhysicalDesignData;
	friend class PhysicalViaGeometry;
	friend class PhysicalViaRuleBase;
	friend class PhysicalViaRule;
	friend class PhysicalViaRuleGenerate;
	friend class PhysicalVia;
	RSYN_FRIEND_OF_GENERIC_LIST_COLLECTION;
protected:
	//! @brief Constructs a Rsyn::PhysicalLayer object with a pointer to Rsyn::PhysicalLayerData.
	PhysicalLayer(PhysicalLayerData * data) : Proxy(data) {	}
public:
	//! @brief Constructs a Rsyn::PhysicalLayer object with a null pointer to Rsyn::PhysicalLayerData.
	PhysicalLayer() : Proxy(nullptr) {}
	//! @brief Constructs a Rsyn::PhysicalLayer object with a null pointer to Rsyn::PhysicalLayerData.
	PhysicalLayer(std::nullptr_t) : Proxy(nullptr) {}
	
	//! @brief Returns the Rsyn::PhysicalLayer layer.
	lefiLayer* getLayer() const;
	//! @brief Returns the Rsyn::PhysicalLayer name.
	std::string getName() const;
	//! @brief Returns the layer type (ROUTING, CUT, OVERLAP, MASTERSLICE, or IMPLANT).
	Rsyn::PhysicalLayerType getType() const;
	//! @brief Returns the layer width in DBU.
	DBU getWidth() const;
	//! @brief Returns the layer index w.r.t to all physical layers. It starts
	//!        from 0 that is the bottom layer
	//! @note  If you need an index relative to the layer type, see
	//!        getRelativeIndex().
	int getIndex() const;
	//! @brief Returns the relative layer index w.r.t. the layers of same type.
	int getRelativeIndex() const;
	
	Rsyn::PhysicalLayer getPhysicalLayerLower() const;
	Rsyn::PhysicalLayer getPhysicalLayerUpper() const;
	
}; // end class 

} // end namespace 


#endif /* PHYSICALDESIGN_PHYSICALLAYER_H */
