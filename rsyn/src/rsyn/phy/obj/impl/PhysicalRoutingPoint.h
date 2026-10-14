// 文件功能：实现路径点的位置、延伸量和过孔标记读取。
// 【零基础阅读】实现路径点的坐标、延伸、过孔和可选属性判断。
// has* 为真时才应把相应属性当成输入提供的有效资料。
// 过孔连接改变后续路径所在层，导线延伸改变金属边界，这些都影响占用几何。
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
 * File:   PhysicalRoutingPoint.h
 * Author: jucemar
 *
 * Created on 23 de Maio de 2017, 20:52
 */

namespace Rsyn {
// clsPos 点坐标，clsExtension 延伸 DBU，clsVia 型号，clsHas* 表明可选几何/光罩/虚拟项是否提供。

// -----------------------------------------------------------------------------

inline DBUxy PhysicalRoutingPoint::getPosition() const {
	return data->clsPos;
} // end method 

// -----------------------------------------------------------------------------

inline DBU PhysicalRoutingPoint::getPosition(const Dimension dim) const {
	return data->clsPos[dim];
} // end method 

// -----------------------------------------------------------------------------

inline DBU PhysicalRoutingPoint::getExtension() const {
	return data->clsExtension;
} // end method 

// -----------------------------------------------------------------------------

inline Rsyn::PhysicalVia PhysicalRoutingPoint::getVia() const {
	return data->clsVia;
} // end method 

// -----------------------------------------------------------------------------

inline const Bounds & PhysicalRoutingPoint::getRectangle() const {
	return data->clsRectangle;
} // end method 

// -----------------------------------------------------------------------------

inline bool PhysicalRoutingPoint::hasExtension() const {
	return data->clsExtension > -1;
} // end method 

// -----------------------------------------------------------------------------

inline bool PhysicalRoutingPoint::hasRectangle() const {
	return data->clsHasRectangle;
} // end method 

// -----------------------------------------------------------------------------

inline bool PhysicalRoutingPoint::hasVia() const {
	return data->clsVia != nullptr;
} // end method 

// -----------------------------------------------------------------------------

} // end namespace 
