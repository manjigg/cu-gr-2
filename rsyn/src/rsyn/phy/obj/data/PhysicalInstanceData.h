// 文件功能：保存物理实例的位置、朝向、边界及逻辑/库关联。
// 【零基础阅读】实际实例既有逻辑身份，也有放置位置/朝向和占用边界。
// 物理记录与逻辑实例关联，库关联说明实例使用哪种尺寸与形状模板。
// 朝向改变引脚几何，所以不能只把原库形状平移就当成所有实例的实际形状。
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
 * File:   PhysicalCellData.h
 * Author: jucemar
 *
 * Created on 12 de Setembro de 2016, 21:01
 */

#ifndef PHYSICALDESIGN_PHYSICALINSTANCEDATA_H
#define PHYSICALDESIGN_PHYSICALINSTANCEDATA_H
namespace Rsyn {
// clsBlock 大块标志，clsPlaced 已放置，clsPort 端口，clsHasLayerBounds 层定义边界。
// clsInitialPos 初始 DBU 放置点，clsPortLayer 端口层，clsInstance 对应逻辑实例；初始点非当前更新点。

class PhysicalInstanceData {
	friend class PhysicalDesign;
public:
	bool clsBlock : 1;
	bool clsPlaced : 1;
	bool clsPort : 1;
	bool clsHasLayerBounds : 1; //  the bounds of a cell is defined by one of the layers. 

	DBUxy clsInitialPos; // Initial position from global placement when in detailed placement has maximal displacement
	PhysicalLayer clsPortLayer;
	Rsyn::Instance clsInstance;

	PhysicalInstanceData() {
		clsBlock = false;
		clsPlaced = false;
		clsPort = false;
		clsHasLayerBounds = false;
	} // end constructor 
}; // end class

} // end namespace 


#endif /* PHYSICALDESIGN_PHYSICALINSTANCEDATA_H */
