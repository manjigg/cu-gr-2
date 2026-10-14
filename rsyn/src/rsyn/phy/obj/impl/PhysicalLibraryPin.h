// 文件功能：实现库引脚的边界和几何集合读取。
// 【零基础阅读】返回库引脚的边界与形状组，坐标仍需与实例变换配合。
// 整体边界覆盖所有形状，却不保证边界内每一点都有引脚金属。
// 真正接入候选应依据具体矩形；这正是 CUGR 后续遍历几何的原因。
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

namespace Rsyn {
// clsPhysicalPinGeometries 为各组局部形状；clsLayerBound 是边界摘要，不能据此假定内部全被金属覆盖。

// -----------------------------------------------------------------------------

inline PhysicalLibraryCell PhysicalLibraryPin::getPhysicalLibraryCell() const {
	return data->clsLibraryCell;
} // end method

// -----------------------------------------------------------------------------

inline Bounds PhysicalLibraryPin::getICCADBounds() {
	return data->clsLayerBound;
} // end method 

// -----------------------------------------------------------------------------

inline const Bounds & PhysicalLibraryPin::getICCADBounds() const {
	return data->clsLayerBound;
} // end method 

// -----------------------------------------------------------------------------

inline std::vector<PhysicalPinGeometry> & PhysicalLibraryPin::allPinGeometries() {
	return data->clsPhysicalPinGeometries;
} // end method 

// -----------------------------------------------------------------------------

inline const std::vector<PhysicalPinGeometry> & PhysicalLibraryPin::allPinGeometries() const {
	return data->clsPhysicalPinGeometries;
} // end method 

// -----------------------------------------------------------------------------

inline std::size_t PhysicalLibraryPin::getNumPinGeometries() const {
	return data->clsPhysicalPinGeometries.size();
} // end method 

// -----------------------------------------------------------------------------

inline bool PhysicalLibraryPin::hasPinGeometries() const {
	return !data->clsPhysicalPinGeometries.empty();
} // end method 

// -----------------------------------------------------------------------------

inline bool PhysicalLibraryPin::isEmptyPinGeometries() const {
	return data->clsPhysicalPinGeometries.empty();
} // end method 

// -----------------------------------------------------------------------------

inline PhysicalPinDirection PhysicalLibraryPin::getPinDirection() const {
	return data->clsDirection;
} // end method 

// -----------------------------------------------------------------------------

inline PhysicalPinUse PhysicalLibraryPin::getUse () const {
	return data->clsUse;
} // end method 

// -----------------------------------------------------------------------------

} // end namespace
