// 文件功能：实现按工艺层组织和查找过孔的方法。
// 【零基础阅读】方法从按层组织的资料中取得关联过孔，避免逐个扫描全部过孔。
// -> 从底层数据指针取成员；返回代理让调用方继续查形状/规则。
// 这里负责查询已有资料，最终选哪只过孔由调用它的算法决定。
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
 * File:   LayerViaManager.h
 * Author: jucemar
 *
 * Created on February 3, 2018, 9:31 AM
 */
namespace Rsyn {
// layer 是字典键，data->clsVias/BottomVias/TopVias 对应该层的候选过孔表，返回的不是唯一选定过孔。

inline const std::vector<Rsyn::PhysicalVia> & LayerViaManager::allVias(Rsyn::PhysicalLayer layer) const {
	return data->clsVias[layer];
} // end method 

// -----------------------------------------------------------------------------

inline const std::vector<Rsyn::PhysicalVia> & LayerViaManager::allBottomVias(Rsyn::PhysicalLayer layer) const {
	return data->clsBottomVias[layer];
} // end method 

// -----------------------------------------------------------------------------

inline const std::vector<Rsyn::PhysicalVia> & LayerViaManager::allTopVias(Rsyn::PhysicalLayer layer) const {
	return data->clsTopVias[layer];
} // end method 

// -----------------------------------------------------------------------------

inline bool LayerViaManager::hasVias(Rsyn::PhysicalLayer layer) const {
	if(layer == nullptr)
		return false;
	if(layer.getType() != Rsyn::PhysicalLayerType::ROUTING)
		return false;
	const std::vector<Rsyn::PhysicalVia> & vias  = data->clsVias[layer];
	return vias.size() > 0;
} // end method 

// -----------------------------------------------------------------------------

inline bool LayerViaManager::hasBottomVias(Rsyn::PhysicalLayer layer) const {
	if(layer == nullptr)
		return false;
	if(layer.getType() != Rsyn::PhysicalLayerType::ROUTING)
		return false;
	const std::vector<Rsyn::PhysicalVia> & vias  = data->clsBottomVias[layer];
	return vias.size() > 0;
} // end method 

// -----------------------------------------------------------------------------

inline bool LayerViaManager::hasTopVias(Rsyn::PhysicalLayer layer) const {
	if(layer == nullptr)
		return false;
	if(layer.getType() != Rsyn::PhysicalLayerType::ROUTING)
		return false;
	const std::vector<Rsyn::PhysicalVia> & vias  = data->clsTopVias[layer];
	return vias.size() > 0;
} // end method 

// -----------------------------------------------------------------------------

} // end namespace 
