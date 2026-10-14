// 文件功能：保存过孔规则的层、尺寸、间距与包围参数。
// 【零基础阅读】规则像“怎样做一种过孔”的配方，而 Via 是具体可用过孔描述。
// 配方中的层、切孔宽高、间距和包围决定金属/切孔几何是否满足要求。
// 记录规则本身不表示在芯片某位置放了一只过孔；位置在实际路由数据里。
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
 * File:   ViaRule.h
 * Author: jucemar
 *
 * Created on November 12, 2018, 4:55 PM
 */

#ifndef RSYN_DATABASE_VIARULE_H
#define RSYN_DATABASE_VIARULE_H


namespace Rsyn {
// clsIsDefault/IsGenerate 规则标志，clsHasCutResistance/HasWidth[部位] 可选参数提供标志。
// clsRelativeIndex 规则编号，clsName 规则名，clsLayers[底/切/顶] 层地址。
// clsLayerDirection[底/顶] 方向，clsWidth[底/顶][低/高] DBU 线宽区间；clsEnclosure1/2[部位] 包围量。
// clsCutBounds 切孔矩形，clsCutSpacing[x/y] 孔间隔，clsCutResistance 电阻，clsVias 关联过孔列表。

class ViaRuleData : public PhysicalObject {
public:
	bool clsIsDefault : 1; 
	bool clsIsGenerate : 1;
	bool clsHasCutResistance : 1;
	bool clsHasWidth [NUM_VIA_LEVELS];
	int clsRelativeIndex = -1;
	Rsyn::PhysicalLayerData * clsLayers [NUM_VIA_LAYERS]; // top or bottom layer levels. Cut is only for generate type
	Rsyn::PhysicalLayerDirection clsLayerDirection [NUM_VIA_LEVELS]; // Directions for top or bottom layer levels.
	DBU clsWidth[NUM_VIA_LEVELS][NUM_VIA_RANGES]; // Specifies a wire width range  for bottom and top layer levels. 
	DBU clsEnclosure1 [NUM_VIA_LEVELS];
	DBU clsEnclosure2 [NUM_VIA_LEVELS];
	Bounds clsCutBounds;
	DBU clsCutSpacing [2] = {0, 0}; // xCutSpacing, yCutSpacing
	float clsCutResistance = 0.0;
	std::vector<Rsyn::PhysicalVia>  clsVias;
//	std::vector<Rsyn::Property> clsProperties; // TODO
	std::string clsName; // Via rule name

	
	
	void init() {
		clsIsDefault = false;
		clsIsGenerate = false;
		clsHasCutResistance = false;
		for(int i = 0; i < NUM_VIA_LAYERS; ++i) {
			clsLayers[i] = nullptr;
		} // end for 
		for(int i = 0; i < NUM_VIA_LEVELS; ++i) {
			clsHasWidth[i] = false;
			clsLayerDirection[i] = UNKNOWN_PREFERRED_DIRECTION;
			for(int j = 0; j < NUM_VIA_RANGES; ++j) {
				clsWidth[i][j] = -std::numeric_limits<DBU>::max();
			} // end for 
		} // end for 
	} // end method 
	
	ViaRuleData() = default;
}; // end class 

} // end namespace 
#endif /* RSYN_DATABASE_VIARULE_H */
