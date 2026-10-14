// 文件功能：声明过孔规则的公共名称、层和类别接口。
// 【零基础阅读】公共规则基类提供名称、类别和通用层资料，派生规则增加专用字段。
// 继承像共用配方封面，固定规则与生成式规则的具体条款不同。
// public 方法是查询菜单，底层规则字段在 ViaRuleData 中保存。
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
 * File:   PhysicalViaRuleBase.h
 * Author: jucemar
 *
 * Created on November 13, 2018, 3:37 PM
 */

#ifndef RSYN_DATABASE_PHYSICALVIARULEBASE_H
#define RSYN_DATABASE_PHYSICALVIARULEBASE_H

namespace Rsyn {
// data 指 ViaRuleData，clsRelativeIndex 规则编号，clsLayers 底/切/顶层；不是已放置过孔的坐标。

class PhysicalViaRuleBase : public Proxy<ViaRuleData> {
	friend class PhysicalDesign;
	friend class PhysicalVia;
	RSYN_FRIEND_OF_GENERIC_LIST_COLLECTION;
protected:
	//! @brief Constructs a Rsyn::PhysicalViaRule object with a pointer to Rsyn::ViaRuleData.

	PhysicalViaRuleBase(ViaRuleData * data) : Proxy(data) {
	}
public:
	//! @brief Constructs a Rsyn::PhysicalViaRule object with a null pointer to Rsyn::ViaRuleData.

	PhysicalViaRuleBase() : Proxy(nullptr) {
	}
	//! @brief Constructs a Rsyn::PhysicalViaRule object with a null pointer to Rsyn::ViaRuleData.

	PhysicalViaRuleBase(std::nullptr_t) : Proxy(nullptr) {
	}
	
	//! @brief Returns the via rule base index 
	int getIndex() const;
	
	//! @brief Returns the via rule base as via rule generate
	PhysicalViaRule asViaRule() const;
	//! @brief Returns the via rule base as via rule generate
	PhysicalViaRuleGenerate asViaRuleGenerate() const;
	
	//! @brief Returns if via rule base is only via rule
	bool isViaRule() const;
	//! @brief Returns if the via rule is generate
	bool isViaRuleGenerate() const;
	
	//! @brief Returns the via rule name
	const std::string & getName() const;
	
	//! @brief Returns the min and max range of the top or bottom routing layer directions in the via rule has been defined.
	bool hasWidth(const Rsyn::ViaLevel level) const;
	//! @brief Returns the min and max range of the top or bottom routing layer directions in the via rule.
	DBU getWidth(const Rsyn::ViaLevel level, const Rsyn::ViaRange range) const;
}; // end class

} // end namespace


#endif /* RSYN_DATABASE_PHYSICALVIARULEBASE_H */
