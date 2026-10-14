// 文件功能：实现过孔规则类别判断和固定/生成式代理转换。
// 【零基础阅读】实现规则类别判断和代理转换，让固定/生成式配方按正确方式访问。
// 转换是在已有规则上换一个接口视角，不是在芯片上生成一个过孔实例。
// 阅读分支先看规则类型，再看对应返回对象和适用参数。
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
 * Created on November 12, 2018, 9:24 PM
 */

namespace Rsyn {
// clsIsGenerate 选择生成/固定代理，clsIsDefault 默认标志；类型转换不创建实际过孔几何。

inline int PhysicalViaRule::getRelativeIndex() const {
	return data->clsRelativeIndex;
} // end method 

// -----------------------------------------------------------------------------

inline Rsyn::PhysicalLayer PhysicalViaRule::getLayer(const Rsyn::ViaLevel level) const {
	return Rsyn::PhysicalLayer(data->clsLayers[level]);
} // end method 

// -----------------------------------------------------------------------------

inline Rsyn::PhysicalLayerDirection PhysicalViaRule::getLayerDirection(const Rsyn::ViaLevel level) const {
	return data->clsLayerDirection[level];
} // end method 

// -----------------------------------------------------------------------------

inline const std::vector<Rsyn::PhysicalVia> & PhysicalViaRule::allVias() const {
	return data->clsVias;
} // end method 

// -----------------------------------------------------------------------------

}
