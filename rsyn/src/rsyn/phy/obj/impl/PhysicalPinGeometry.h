// 文件功能：实现引脚几何组的层集合和属性读取。
// 【零基础阅读】把引脚几何组的层集合交给调用方，继续取得各层形状。
// 几何组是组织结构，里面可能有多个层/矩形，不是一个最终接入点。
// 调用方的嵌套 for 对应这些层级，本方法只是返回相应集合。
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
 * File:   PhysicalPinGeometry.h
 * Author: jucemar
 *
 * Created on 18 de Setembro de 2016, 08:50
 */

namespace Rsyn {
// clsPinLayers 是当前组内层对象列表，clsPinPortClass 是组分类；列表顺序不等于工艺层号。

inline PhysicalPinGeometryClass PhysicalPinGeometry::getPinGeometryClass() const {
	return data->clsPinPortClass;
} // end method 

// -----------------------------------------------------------------------------

inline bool PhysicalPinGeometry::hasPinLayer() const {
	return !data->clsPinLayers.empty();
} // end method 

// -----------------------------------------------------------------------------

inline std::size_t PhysicalPinGeometry::getNumPinLayers() const{
	return data->clsPinLayers.size();
} // end method 

// -----------------------------------------------------------------------------

inline const std::vector<Rsyn::PhysicalPinLayer> &  PhysicalPinGeometry::allPinLayers() const {
	return data->clsPinLayers;
} // end method 

// -----------------------------------------------------------------------------

inline Rsyn::PhysicalPinLayer PhysicalPinGeometry::getLowerPinLayer() const {
	if(data->clsPinLayers.empty())
		return Rsyn::PhysicalPinLayer(nullptr);
	return data->clsPinLayers.front();
} // end method 

// -----------------------------------------------------------------------------

inline Rsyn::PhysicalPinLayer PhysicalPinGeometry::getUpperPinLayer() const {
	if(data->clsPinLayers.empty())
		return Rsyn::PhysicalPinLayer(nullptr);
	return data->clsPinLayers.back();

} // end method 

// -----------------------------------------------------------------------------

} // end namespace
