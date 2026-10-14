// 文件功能：保存工艺层类型、索引、规则和 LEF 层对象。
// 【零基础阅读】一层工艺资料说明它是金属布线层还是切孔层，以及宽度/规则等信息。
// 编号用于数组定位，名称如 M2 用于文件输出，类型用于判断用途。
// 底层 LEF 对象还提供详细规则；PhysicalLayer 代理包装这些数据供导入器查询。
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
 * File:   PhysicalLayerData.h
 * Author: jucemar
 *
 * Created on 13 de Setembro de 2016, 19:09
 */

#ifndef PHYSICALDESIGN_PHYSICALLAYERDATA_H
#define PHYSICALDESIGN_PHYSICALLAYERDATA_H


namespace Rsyn {
// clsWidth DBU 线宽，clsIndex 全层表编号，clsRelativeIndex 同类别相对编号，clsType 层类别。
// clsLower/clsUpper 相邻层资料地址，nullptr 表示没有该侧层。

class PhysicalLayerData : public PhysicalObject {
public:
    lefiLayer* clsLayer = nullptr;
	DBU clsWidth = 0;
	int clsIndex = -1;
	int clsRelativeIndex = -1;
	Rsyn::PhysicalLayerType clsType = INVALID_PHY_LAYER_TYPE;
	std::string clsName = Rsyn::getPhysicalInvalidName();
	Rsyn::PhysicalLayerData * clsLower = nullptr;
	Rsyn::PhysicalLayerData * clsUpper = nullptr;
	PhysicalLayerData() = default;
}; // end class 

} // end namespace 


#endif /* PHYSICALDESIGN_PHYSICALLAYERDATA_H */
