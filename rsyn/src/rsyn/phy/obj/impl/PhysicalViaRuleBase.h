// 文件功能：实现过孔规则的名称、类型和公共层参数读取。
// 【零基础阅读】读取规则的公共名称、类别与层相关参数。
// 基类方法供派生规则共用，专用切孔/生成参数在相应派生接口里。
// 工艺规则查询只是取要求，实际布线是否合法需结合具体几何检查。
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
 * Created on November 13, 2018, 3:42 PM
 */

namespace Rsyn {
// data->clsName 规则名、clsRelativeIndex 规则编号、clsLayers 层数组；身份数据不同于位置。

inline int PhysicalViaRuleBase::getIndex() const {
	return data->id;
} // end method 

// -----------------------------------------------------------------------------

inline PhysicalViaRule PhysicalViaRuleBase::asViaRule() const {
	return isViaRule() ? PhysicalViaRule(data) : PhysicalViaRule();
} // end method 

// -----------------------------------------------------------------------------

inline PhysicalViaRuleGenerate PhysicalViaRuleBase::asViaRuleGenerate() const {
	return isViaRuleGenerate() ? PhysicalViaRuleGenerate(data) : PhysicalViaRuleGenerate();
} // end method 

// -----------------------------------------------------------------------------

inline bool PhysicalViaRuleBase::isViaRule() const {
	return !isViaRuleGenerate();
} // end method 

// -----------------------------------------------------------------------------

inline bool PhysicalViaRuleBase::isViaRuleGenerate() const {
	return data->clsIsGenerate;
} // end method 

// -----------------------------------------------------------------------------

inline const std::string & PhysicalViaRuleBase::getName() const {
	return data->clsName;
} // end method 

// -----------------------------------------------------------------------------

inline bool PhysicalViaRuleBase::hasWidth(const Rsyn::ViaLevel level) const {
	return data->clsHasWidth[level];
} // end method 

// -----------------------------------------------------------------------------

inline DBU PhysicalViaRuleBase::getWidth(const Rsyn::ViaLevel level, const Rsyn::ViaRange range) const {
	return data->clsWidth[level][range];
} // end method 

// -----------------------------------------------------------------------------

} // end namespace
