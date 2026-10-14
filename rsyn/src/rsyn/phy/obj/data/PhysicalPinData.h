// 文件功能：保存实例物理引脚的几何和位置关联。
// 【零基础阅读】实例物理引脚回答“这个实际接线端在哪里”，不同于库引脚模板。
// 数据与实际逻辑引脚关联，通过实例位置/模板几何可得到所需物理信息。
// 本文件存记录，PhysicalPin 代理提供查询，路径选择由路由模块另做。
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
 * File:   PhysicalPinData.h
 * Author: jucemar
 *
 * Created on 14 de Setembro de 2016, 21:24
 */

#ifndef PHYSICALDESIGN_PHYSICALPINDATA_H
#define PHYSICALDESIGN_PHYSICALPINDATA_H

namespace Rsyn {
// clsDisplacement DBU 相对位移，clsPhysicalPinGeometries 形状组表。
// clsDirection 信号方向，clsLayerBound 指定层边界；不是路由器已选定的网格接入点。

class PhysicalPinData {
public:
	DBUxy clsDisplacement;
	//! @details A pin may have several ports. However, each port is weakly connected to other. 
	//! It is assumed that they have high resistance among them. 
	std::vector<PhysicalPinGeometry> clsPhysicalPinGeometries;
	PhysicalPinDirection clsDirection = PIN_INVALID_DIRECTION;
	Bounds clsLayerBound; // The pin bounds is defined by one of the layers
	PhysicalPinData() = default;
}; // end class 

} // end namespace 

#endif /* PHYSICALDESIGN_PHYSICALPINDATA_H */
