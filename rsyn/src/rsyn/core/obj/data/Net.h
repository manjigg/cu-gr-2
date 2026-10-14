// 文件功能：保存线网连接的引脚、驱动关系和网表遍历所需状态。
// 【零基础阅读】逻辑线网记录哪些接线端属于同一信号，尚未决定线走哪里。
// 驱动/负载指信号来源和接收者，物理布线要让这些引脚真正连通。
// 字段存引脚及其关联，不是存一串最终金属坐标；物理路径另由 phy/路由模块表示。
// 同名 data/decl/impl 三文件分别负责存储、访问约定和具体访问方法。
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
// 【字段】pins 实际端点表，driver 驱动端；numPinsOfType[信号方向] 计数。
// sign 遍历标记，netUse 信号/电源用途，均不是坐标或金属层编号。

struct NetTagData {
	TristateFlag ideal;
	NetTypeTag type;

	NetTagData() :
		type(NET_TYPE_TAG_NOT_SPECIFIED) {
	} // end constructor
}; // end struct

// -----------------------------------------------------------------------------

struct NetData : ObjectData {
	// Index in parent module.
	Index mid;

	// User tags.
	NetTagData tag;
	
	// Using a vector for fast traverse, but slow insertion and removal.
	std::vector<Pin> pins;
	
	// Driver. If multiple-drivers, store one of them without any assumptions.
	Pin driver;
	
	// Parent
	Module parent;
	
	std::array<int, NUM_SIGNAL_DIRECTIONS> numPinsOfType;
	
	// Helper used for netlist traversals.
	int sign;
	
	// Mateus @ 20190204: Adding net use;
	Use netUse;
	
	NetData() : 
		mid(-1),
		sign(-1),
		driver(nullptr), 
		numPinsOfType({0, 0, 0, 0}),
		parent(nullptr),
		netUse(UNKNOWN_USE) {
	} // end constructor	
}; // end struct

} // end namespace
