// 文件功能：保存设计级对象集合、名称索引和管理状态，支撑网表对象创建与查询。
// 【零基础阅读】设计数据像总账本，管理所有实例、线网、库对象及名称索引。
// 名称索引用来把 "U1" 等文字换成对象，而集合用于逐个遍历设计里的对象。
// 本层是真实存储；Design 代理只是访问它的入口，复制代理通常不会复制整份设计。
// 读成员时先问“它管理哪类对象”，再看 decl/Design.h 的创建/查询接口。
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
#include "utils/robin_hood.h"
 
namespace Rsyn {
// 【字段索引】name 设计名，topModule 顶层；instances/pins/arcs/nets 实际数据集合，cells 单元代理表。
// libraryCells/Pins/Arcs 型号级模板；instanceNames/netNames 名字表；anonymousInstanceId/NetId 匿名计数。
// instanceMapping/netMapping/libraryCellMapping：“名称->代理”字典，非几何表。
// portLibraryCells[方向] 端口模板，ports[方向] 端口表，instanceCount[类型] 实例计数。
// structuralStartpoints/Endpoints 结构起终引脚，netsInTopologicalOrder 拓扑排序网表。
// dirty 缓存待刷新，initialized 已初始化，sign 遍历标记，observers[事件] 观察者列表。

struct DesignData {  
	std::string name;

	Rsyn::Module topModule;

	List<InstanceData> instances;
	List<PinData> pins;
	List<ArcData> arcs;
	List<NetData> nets;
        List<Cell> cells;
	
	List<LibraryCellData> libraryCells;
	List<LibraryPinData> libraryPins;
	List<LibraryArcData> libraryArcs;

	std::vector<std::string> instanceNames;
	std::vector<std::string> netNames;
	
	int anonymousInstanceId;
	int anonymousNetId;
	
	robin_hood::unordered_map<std::string, Instance> instanceMapping;
	robin_hood::unordered_map<std::string, Net> netMapping;
	robin_hood::unordered_map<std::string, LibraryCell> libraryCellMapping;
	
	std::array<LibraryCell, NUM_SIGNAL_DIRECTIONS> portLibraryCells;
	std::set<Cell> ports[NUM_SIGNAL_DIRECTIONS];
	
	std::array<int, NUM_INSTANCE_TYPES> instanceCount;
	
	std::vector<Pin> structuralStartpoints;
	std::vector<Pin> structuralEndpoints;	
	std::vector<Net> netsInTopologicalOrder;
	
	bool dirty;	
	bool initialized;
	
	// Used for some netlist traversing (e.g. update topological ordering)...
	int sign;	
	
	// Observers
	std::array<std::list<DesignObserver *>, NUM_DESIGN_EVENTS> observers;
	
	// Constructor
	DesignData() :
		initialized(false),
		dirty(false),
		anonymousInstanceId(0),
		anonymousNetId(0),
		instanceCount({0, 0, 0}),
		sign(0) {
	} // end constructor
}; // end class

} // end namespace
