// 文件功能：声明过孔规则代理及固定/生成式规则转换接口。
// 【零基础阅读】规则代理根据类别提供固定/生成式过孔规则的访问方式。
// 先判断类别再转换，避免把不适用的参数当有效值。
// 规则是工艺配方，不带某只实例过孔在芯片上的实际放置位置。
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
 * File:   PhysicalViaRule.h
 * Author: jucemar
 *
 * Created on November 12, 2018, 5:53 PM
 */

#ifndef RSYN_DATABASE_PHYSICALVIARULE_H
#define RSYN_DATABASE_PHYSICALVIARULE_H

namespace Rsyn {
// clsIsGenerate 判断规则类型，clsIsDefault 默认标志，clsName 规则名；转换前先确认类型。

class PhysicalViaRule : public PhysicalViaRuleBase {
	friend class PhysicalDesign;
	friend class PhysicalViaRuleBase;
	RSYN_FRIEND_OF_GENERIC_LIST_COLLECTION;
protected:
	//! @brief Constructs a Rsyn::PhysicalViaRule object with a pointer to Rsyn::ViaRuleData.

	PhysicalViaRule(ViaRuleData * data) : PhysicalViaRuleBase(data) {
	}
public:
	//! @brief Constructs a Rsyn::PhysicalViaRule object with a null pointer to Rsyn::ViaRuleData.

	PhysicalViaRule() : PhysicalViaRuleBase(nullptr) {
	}
	//! @brief Constructs a Rsyn::PhysicalViaRule object with a null pointer to Rsyn::ViaRuleData.

	PhysicalViaRule(std::nullptr_t) : PhysicalViaRuleBase(nullptr) {
	}
	
	//! @brief Returns the via rule relative index
	int getRelativeIndex() const;
	
	//! @brief Returns the top or bottom routing layers in the via rule.
	Rsyn::PhysicalLayer getLayer(const Rsyn::ViaLevel level) const;
	//! @brief Returns the top or bottom routing layer directions in the via rule.
	Rsyn::PhysicalLayerDirection getLayerDirection(const Rsyn::ViaLevel level) const;
	//! @brief Returns all cut layers defined in via rule.
	const std::vector<Rsyn::PhysicalVia> & allVias() const;

}; // end class

} // end namespace

#endif /* RSYN_DATABASE_PHYSICALVIARULE_H */
