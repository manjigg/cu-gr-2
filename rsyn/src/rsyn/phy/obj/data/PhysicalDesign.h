// 文件功能：保存物理设计对象集合与逻辑对象到物理属性的映射。
// 【零基础阅读】物理设计总账本保存尺寸、位置、工艺层和布局集合，不只是逻辑名单。
// 属性映射把逻辑单元/引脚关联到物理记录，才能从引脚找到其实际形状。
// 数据在这里，查询接口在 decl/PhysicalDesign.h，导入主要在 impl 的 .cpp。
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
#ifndef RSYN_PHYSICALDESIGN_PHYSICALDESIGNDATA_H
#define RSYN_PHYSICALDESIGN_PHYSICALDESIGNDATA_H


#include "utils/robin_hood.h"


namespace Rsyn {
// 【字段字典】cls 是成员命名前缀，不是单位；clsDesign/clsModule 关联逻辑设计/顶层。
// clsLayerViaManager 按层查过孔；callbackAddCreatePhysicalCell/OnPreCellRemoveEvent 创建/移除前响应函数表。
// clsPhysicalLibraryPins/Pins/Instances/LibraryCells/Nets：“逻辑对象->物理资料”属性映射。
// clsPhysicalRows/Layers/Spacing 行/层/间距表；Regions/Groups/Sites/Vias 区域/组/site/过孔表。
// clsPhysicalViaRuleBases/ViaRules/ViaRuleGenerates 公共/固定/生成式规则表。
// clsPhysicalSpecialNets/Tracks/GCell/RoutingGrids 特殊网/轨道组/粗网格定义/物理轨道网格表。
// clsMapPhysical*：“名称->列表编号”字典，clsMapLayerToTracks/ToRoutingGrid：“层->关联对象”。
// clsPhysicalRoutingLayerIndeces 布线层在全层表的编号；Indeces 是源码原拼写。
// clsPhysicalDie 裸片，clsTotalAreas[物理类型] 面积，clsNumElements[类型] 数量，clsNumLayers[层类别] 层数。
// clsHPWL.x/y 总包围盒两轴线长，clsDBUs[单位类别] DBU 换算尺度。
// clsLoadDesign 加载状态，clsEnablePhysicalPins/MergeRectangles/NetPinBoundaries 功能开关。
// clsClkNet 时钟网，clsMode 加载模式，clsPhysicalObservers[事件类别] 响应者列表。

class PhysicalDesignData : public PhysicalObject {
	friend class PhysicalDesign;
public:
	Rsyn::Design clsDesign;
	Rsyn::Module clsModule;
	Rsyn::LayerViaManagerData clsLayerViaManager;

	std::list<std::function<void(Rsyn::Instance cell) >> callbackAddCreatePhysicalCell;
	std::list<std::function<void(Rsyn::Instance cell) >> callbackOnPreCellRemoveEvent;
	Rsyn::Attribute<Rsyn::LibraryPin, PhysicalLibraryPinData> clsPhysicalLibraryPins;
	Rsyn::Attribute<Rsyn::Pin, PhysicalPinData> clsPhysicalPins;
	Rsyn::Attribute<Rsyn::Instance, PhysicalInstanceData> clsPhysicalInstances;
	Rsyn::Attribute<Rsyn::LibraryCell, PhysicalLibraryCellData> clsPhysicalLibraryCells;
	Rsyn::Attribute<Rsyn::Net, PhysicalNetData> clsPhysicalNets;
	Rsyn::List<PhysicalRowData> clsPhysicalRows;
	Rsyn::List<PhysicalLayerData> clsPhysicalLayers;
	Rsyn::List<PhysicalSpacingData> clsPhysicalSpacing;
	std::vector<PhysicalRegion> clsPhysicalRegions;
	std::vector<PhysicalGroup> clsPhysicalGroups;
	std::vector<PhysicalSite> clsPhysicalSites;
	std::vector<PhysicalVia> clsPhysicalVias;
	std::vector<Rsyn::PhysicalViaRuleBase> clsPhysicalViaRuleBases;
	std::vector<Rsyn::PhysicalViaRule> clsPhysicalViaRules;
	std::vector<Rsyn::PhysicalViaRuleGenerate> clsPhysicalViaRuleGenerates;
	std::vector<PhysicalSpecialNet> clsPhysicalSpecialNets;
	std::vector<PhysicalTracks> clsPhysicalTracks;
	std::vector<PhysicalGCell> clsPhysicalGCell;
	std::vector<Rsyn::PhysicalRoutingGrid> clsPhysicalRoutingGrids;
	robin_hood::unordered_map<std::string, int> clsMapPhysicalSites;
	robin_hood::unordered_map<std::string, std::size_t> clsMapPhysicalRegions;
	robin_hood::unordered_map<std::string, std::size_t> clsMapPhysicalGroups;
	robin_hood::unordered_map<std::string, std::size_t> clsMapPhysicalSpecialNets;
	std::map<Rsyn::PhysicalLayer, std::vector<Rsyn::PhysicalTracks>> clsMapLayerToTracks;
	std::map<Rsyn::PhysicalLayer, Rsyn::PhysicalRoutingGrid> clsMapLayerToRoutingGrid;
	
	//From LEF file
	robin_hood::unordered_map<std::string, std::size_t> clsMapPhysicalLayers;
	robin_hood::unordered_map<std::string, std::size_t> clsMapPhysicalVias;
	robin_hood::unordered_map<std::string, std::size_t> clsMapPhysicalViaRuleBases;
	std::vector<std::size_t> clsPhysicalRoutingLayerIndeces;
	//std::vector<PhysicalSpacing> clsPhysicalSpacing;

	Rsyn::PhysicalDieData clsPhysicalDie; // total area of the circuit including core bound. 

	DBU clsTotalAreas[NUM_PHYSICAL_TYPES];
	int clsNumElements[NUM_PHYSICAL_TYPES];
	int clsNumLayers[NUM_PHY_LAYER];

	DBUxy clsHPWL;
	DBU clsDBUs[NUM_DBU]; // LEF and DEF data base units resolution and DEF/LEF multiplier factor

	bool clsLoadDesign : 1;
	bool clsEnablePhysicalPins : 1;
	bool clsEnableMergeRectangles : 1;
	bool clsEnableNetPinBoundaries : 1;

	Rsyn::Net clsClkNet;
	
	// Physical design mode 
	PhysicalDesignMode clsMode = PhysicalDesignMode::ALL;
		
	////////////////////////////////////////////////////////////////////////////
	// Observerss
	////////////////////////////////////////////////////////////////////////////

	std::array<std::list<PhysicalDesignObserver *>, NUM_PHYSICAL_EVENTS> clsPhysicalObservers;

	PhysicalDesignData() : clsClkNet(nullptr), clsDesign(nullptr), clsModule(nullptr) {

		clsLoadDesign = false;
		clsEnablePhysicalPins = false;
		clsEnableMergeRectangles = false;
		clsEnableNetPinBoundaries = false;
		for (int index = 0; index < NUM_DBU; index++) {
			clsDBUs[index] = 0;
		} // end for 
		for (int index = 0; index < NUM_PHYSICAL_TYPES; index++) {
			clsTotalAreas[index] = 0.0;
			clsNumElements[index] = 0;
		} // end for  
		for (int index = 0; index < NUM_PHY_LAYER; index++) {
			clsNumLayers[index] = 0;
		}
	} // end constructor 
}; // end class 

} // end namespace

#endif
