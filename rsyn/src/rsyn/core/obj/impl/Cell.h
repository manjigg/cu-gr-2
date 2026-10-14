// 文件功能：实现 Cell 代理的库单元访问、引脚映射和单元属性查询。
// 【零基础阅读】Cell 方法把实例底层数据与库模板联系起来，回答型号和引脚问题。
// 读一段方法时先看返回类型，再看 data->... 是从哪个关联取结果。
// 同型号单元引用同一库定义，但每个实际实例仍有自己的身份和连接。
// 这些访问方法不选路，CUGR 通过它们取原始网表和库信息。
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
// data 为实例资料地址，lcell 连接模板，pin 逐端点查询；库引脚下标与实际全局 id 不同。
// 返回引用/代理避免复制整库，位置/模板等查询不增加路由 demand。

inline
const std::string &
Cell::getLibraryCellName() const {
	return data->lcell->name;
} // end method

// -----------------------------------------------------------------------------

inline
LibraryCell
Cell::getLibraryCell() const {
	return data->lcell;
} // end method

// -----------------------------------------------------------------------------

inline
Pin
Cell::getPinByLibraryPin(const LibraryPin lpin) const {
	return data->pins[lpin.data->index];
} // end method
	
// -----------------------------------------------------------------------------

inline
void 
Cell::remap(LibraryCell libraryCell) {
	getDesign().remap(*this, libraryCell);
} // end method

// -----------------------------------------------------------------------------

inline
void 
Cell::remap(const std::string &libraryCellName) {
	getDesign().remap(*this, libraryCellName);
} // end method

} // end namespace
