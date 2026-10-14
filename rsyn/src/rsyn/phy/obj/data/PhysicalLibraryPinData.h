// 文件功能：保存库引脚的几何集合和逻辑库引脚关联。
// 【零基础阅读】一个库引脚可由多个形状组成，可能跨层，所以需要几何集合。
// 这些形状在模板坐标中，实例查询时还需处理朝向与放置位移。
// 逻辑关联提供引脚身份，物理集合提供金属在哪里，二者配合才能生成接入点。
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
 * File:   PhysicalLibraryPinData.h
 * Author: jucemar
 *
 * Created on 13 de Setembro de 2016, 21:04
 */

#ifndef PHYSICALDESIGN_PHYSICALLIBRARYPINDATA_H
#define PHYSICALDESIGN_PHYSICALLIBRARYPINDATA_H

namespace Rsyn {
// clsLayerBound 指定层 DBU 边界，clsLibraryCell 所属型号，clsUse 用途，clsDirection 信号方向。
// clsPhysicalPinGeometries[i] 第 i 组形状，组内再分层；i 不表示第 i 个独立引脚。

class PhysicalLibraryPinData {
public:
	Bounds clsLayerBound; // The layer bound is defined by one of the layer.
	PhysicalLibraryCell clsLibraryCell;
	PhysicalPinUse clsUse = PIN_INVALID_USE;
	PhysicalPinDirection clsDirection = PIN_INVALID_DIRECTION;
	// A pin may have several ports. However, each port is weakly connected to other. 
	// It is assumed that they have high resistance among them. 
	std::vector<PhysicalPinGeometry> clsPhysicalPinGeometries;
	
	PhysicalLibraryPinData() = default;
}; // class 

} // end namespace 

#endif /* PHYSICALDESIGN_PHYSICALLIBRARYPINDATA_H */
