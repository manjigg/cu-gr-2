// 文件功能：保存实例的类型、层次关系、引脚与位置等公共数据。
// 【零基础阅读】实例是“设计中放了一个什么对象”的公共记录，可能是单元/模块/端口。
// 类型标记决定它能转换为哪个专门代理，引脚与模块关联把它接进层次网表。
// 对象编号/名称描述身份，位置描述放置，连接关系描述信号；这些概念不要混在一起。
// 数据由 Design 等管理，外部通常借助 Instance 代理访问，而不直接改字段。
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
 
namespace Rsyn {
// 【字段】pins/arcs 实际引脚/弧列表，lcell 库单元资料地址，clsBounds DBU 边界。
// clsOrientation 旋转/镜像姿态；父模块和标签等字段服务身份/层次，不是路线资源需求。

struct InstanceTagData {
	TristateFlag fixed;
	TristateFlag block;
}; // end struct

// -----------------------------------------------------------------------------

struct InstanceData : ObjectData {
	InstanceType type;
		
	std::vector<Pin> pins;
	std::vector<Arc> arcs;
	
	// Design (Cached)
	// @todo Remove.
	Design design;
	
	// The module where this instance is instantiated. If only null for the top
	// module.
	Module parent;
	
	// Local index inside it's parent module.
	Index mid;

	// Extra data for different types of instances.
	union {
		void * extra;
		
		ModuleData * moduleData;
		PinData * outerPin;
		LibraryCellData * lcell;
	}; // end union

	// User tag
	InstanceTagData tag;

	// Physical information.
	Bounds clsBounds;
	PhysicalOrientation clsOrientation;
	DBUxy clsPortPos; // only for port to define position. (@todo remove, use bounds instead).

	// Constructor
	InstanceData() : 
		design(nullptr),
		type(UNKNOWN_INSTANCE_TYPE),
		parent(nullptr),
		extra(nullptr),
		mid(-1),
		clsBounds(0, 0, 0, 0),
		clsOrientation(ORIENTATION_N), // @todo set to R0 by default
		clsPortPos(0, 0) {
	} // end constructor
}; // end struct

} // end namespace
