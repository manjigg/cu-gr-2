// 文件功能：保存库单元尺寸、引脚、障碍及 LEF macro 数据。
// 【零基础阅读】库单元物理模板保存一种零件的尺寸、引脚形状和不能穿过的障碍。
// 它是型号级资料，具体放置位置在实例中；同型号实例可共享这份资料。
// macro 是 LEF 的模板对象，CUGR 据此建立指定朝向的 Macro 几何缓存。
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
 * File:   PhysicalLibraryCellData.h
 * Author: jucemar
 *
 * Created on 12 de Setembro de 2016, 22:08
 */

#ifndef PHYSICALDESIGN_PHYSICALLIBRARYCELLDATA_H
#define PHYSICALDESIGN_PHYSICALLIBRARYCELLDATA_H

namespace Rsyn {
// clsMacroSite site 模板，clsSize.x/y DBU 尺寸，clsLayerBoundIndex 定义边界的层编号。
// clsObs 全部局部库障碍，clsTopLayerObs 最高相关障碍；不是实例全局坐标。

class PhysicalLibraryCellData {
public:
	lefiMacro* clsMacro = nullptr;
	PhysicalSite clsMacroSite;
	DBUxy clsSize;
	int clsLayerBoundIndex = -1; // The cell bound is defined by one of the layers.
	std::vector<Rsyn::PhysicalObstacle> clsObs;
	Polygon clsPolygonBounds;
	Rsyn::PhysicalObstacle clsTopLayerObs;
	PhysicalLibraryCellData() = default;

}; // end class 

} // end namespace 

#endif /* PHYSICALDESIGN_PHYSICALLIBRARYCELLDATA_H */
