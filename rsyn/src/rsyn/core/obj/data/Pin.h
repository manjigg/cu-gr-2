// 文件功能：保存实例引脚与所属实例、线网、库引脚和逻辑弧之间的关联。
// 【零基础阅读】实例引脚是具体零件上的接线端，连接某条 Net 并对应一个库引脚。
// 字段把实例、线网和库模板串起来，沿关联即可从引脚找到它属于谁、连接谁。
// Arc 是引脚间逻辑关联；引脚坐标/金属形状不由这份逻辑数据直接表达。
// 查询逻辑字段用 Pin 代理，取几何还需借助 PhysicalDesign 的属性映射。
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
// 【字段】index 库型号内引脚下标，direction 信号方向，boundary 是否边界端，sign 遍历记号。
// instance 所属实例，net 所接网，arcs[遍历类别] 入/出弧表。
// int/bool 后的 :20/:3/:1 是位域宽度，不是字段初始化值，也不是网格数量。

struct PinData : ObjectData {
	// #> bit pack
	int index : 20;           // index of this pin in the library cell.
	Direction direction : 3;  // direction of this pin
	InstanceType type : 3;    // the type of instance that this pin belongs to
	bool boundary : 1;        // Is this pin a port? Pins belonging to both
	                          // Rsyn::PORT and Rsyn::MODULE are marked as boundary
	                          // pins.
	// <#
	
	int sign;
	
	Instance instance;
	Net net;
	std::vector<Arc> arcs[NUM_TRAVERSE_TYPES];
	TopologicalIndex order; // topological ordering

	PinData() : 
		index(-1),
		direction(UNKNOWN_DIRECTION),
		type(UNKNOWN_INSTANCE_TYPE),
		boundary(false),
		sign(0),
		instance(nullptr), 
		net(nullptr), 
		
		order(std::numeric_limits<TopologicalIndex>::quiet_NaN()) {
	} // end constructor
}; // end struct

} // end namespace
