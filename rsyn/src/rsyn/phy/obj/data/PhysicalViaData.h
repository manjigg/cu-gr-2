// 文件功能：保存过孔的上下/切孔层、几何和生成规则参数。
// 【零基础阅读】过孔像楼层间的电梯，包含底层金属、切孔层和顶层金属。
// 固定过孔有具体形状；生成式过孔还可由切孔尺寸、间距、包围等参数描述。
// 本数据保存这些几何/参数，CUGR 从中提取上下层固定占用区域。
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
 * File:   PhysicalViaData.h
 * Author: jucemar
 *
 * Created on 14 de Maio de 2017, 14:49
 */

#ifndef PHYSICALDESIGN_PHYSICALVIADATA_H
#define PHYSICALDESIGN_PHYSICALVIADATA_H

namespace Rsyn {
// clsIsDefault 默认型号，clsIsViaDesign 来自设计；clsHas* 表明规则/行列/原点/偏移等可选项存在。
// clsViaRuleData 配方地址，clsCutSize[轴] 切孔宽高、clsSpacing[轴] 孔间隙，DBU。
// clsEnclosure[底/顶][轴] 包围量，clsOrigin[轴] 原点，clsOffset[底/顶][轴] 偏移量，DBU。
// clsNumRows/Cols 阵列行/列数，clsPattern 图案名，clsCutResistance 切孔电阻，clsName 型号名。
// clsLayers[底/切/顶] 层地址，clsViaGeometries[底/切/顶][i] 局部形状。
// NUM_VIA_LEVELS 的底/顶下标与 NUM_VIA_LAYERS 的底/切/顶下标不可直接互换。

class PhysicalViaData : public PhysicalObject {
public:
	bool clsIsDefault : 1;
	bool clsIsViaDesign : 1;
	bool clsHasViaRule : 1;
	bool clsHasRowCol : 1;
	bool clsHasOrigin : 1;
	bool clsHasOffset : 1;
	bool clsHasPattern : 1;
	bool clsHasCutResistance : 1;
	ViaType clsType = INVALID_VIA_TYPE;
	Rsyn::ViaRuleData * clsViaRuleData = nullptr;
	DBU clsCutSize [2] = {0, 0}; // xCutSize, yCutSize
	DBU clsSpacing [2] = {0, 0}; // xSpacing, ySpacing
	DBU clsEnclosure[NUM_VIA_LEVELS][2]; // bottomXEnclosure, bottomYEnclosure, topXEnclosure, topYEnclosure
	DBU clsOrigin[2] = {0, 0}; // xOrigin, yOrigin
	DBU clsOffset[NUM_VIA_LEVELS][2]; // bottomXOffset, bottomYOffset, topXOffset, topYOffset
	int clsNumRows = 0;
	int clsNumCols = 0;
    std::string clsPattern = "";
	float clsCutResistance = 0.0;
	Rsyn::PhysicalLayerData *clsLayers[NUM_VIA_LAYERS]; // bottom, cut, top
	std::vector<Rsyn::PhysicalViaGeometry>  clsViaGeometries [NUM_VIA_LAYERS]; // bottom, cut, top
	std::string clsName;
	
	PhysicalViaData() {
		clsIsDefault = false;
		clsIsViaDesign = false;
		clsHasViaRule = false;
		clsHasRowCol = false;
		clsHasOrigin = false;
		clsHasOffset = false;
		clsHasPattern = false;
		clsHasCutResistance = false;
	} // end constructor 
}; // end class 

} // end namespace 

#endif /* PHYSICALDESIGN_PHYSICALVIADATA_H */
