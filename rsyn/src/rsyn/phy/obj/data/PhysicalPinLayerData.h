// 文件功能：保存引脚单层的矩形/多边形形状与层引用。
// 【零基础阅读】本记录描述一个引脚在某层上的金属形状，不一定只有一个矩形。
// layer 是所属工艺层，形状集合是平面范围；不能仅凭平面范围判断跨层连通。
// CUGR 当前模板提取主要使用矩形边界，是否支持其他形状要看调用方筛选条件。
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
 * File:   PhysicalPinLayerData.h
 * Author: jucemar
 *
 * Created on 13 de Setembro de 2016, 21:16
 */

#ifndef PHYSICALDESIGN_PHYSICALPINLAYERDATA_H
#define PHYSICALDESIGN_PHYSICALPINLAYERDATA_H


namespace Rsyn {
// clsBounds 为 DBU 矩形列表，clsPolygons 多边形列表，clsLibLayer 所属工艺层。
// 两列表保存不同形状形式，查询先判断 has*，不能一概视为矩形。

class PhysicalPinLayerData : public PhysicalObject {
public:
	std::vector<Bounds> clsBounds;
	std::vector<Polygon> clsPolygons;
	Rsyn::PhysicalLayer clsLibLayer;
	PhysicalPinLayerData() = default;
}; // end class 

} // end namespace 

#endif /* PHYSICALDESIGN_PHYSICALPINLAYERDATA_H */
