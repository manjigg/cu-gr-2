// 文件功能：实现库弧的端点及所属库单元访问，读取对应 LibraryArcData。
// 【零基础阅读】方法沿 LibraryArcData 的关联取出库弧端点/归属。
// 端点是库引脚模板，不是某个单元实例上的实际引脚。
// 返回代理可继续点号查询；不需调用者直接管理底层数据内存。
// inline 方法通常较短，重点是读清每个返回值来自哪个字段。
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
// data->from/to 返回两个库引脚，data->lcell 返回型号；index 仅在该型号弧表范围内。
// inline 函数仅在调用时执行，不会在 include 时预先遍历所有库弧。

inline
Design 
LibraryArc::getDesign() {
	return data->design;
} // end method

// -----------------------------------------------------------------------------

inline
const Design 
LibraryArc::getDesign() const {
	return data->design;
} // end method

// -----------------------------------------------------------------------------

inline
std::string
LibraryArc::getName() const {
	return getFromName() + "->" + getToName();		
} // end method

// -----------------------------------------------------------------------------

inline
std::string
LibraryArc::getFullName() const {
	return getLibraryCell().getName() + ":" + getName();
} // end method

// -----------------------------------------------------------------------------

inline
std::string
LibraryArc::getFromName() const {
	return getFromLibraryPin().getName();
} // end method

// -----------------------------------------------------------------------------

inline
std::string
LibraryArc::getToName() const {
	return getToLibraryPin().getName();
} // end method

// -----------------------------------------------------------------------------

inline
LibraryCell
LibraryArc::getLibraryCell() const {
	return data->lcell;
} // end method

// -----------------------------------------------------------------------------

inline
LibraryPin
LibraryArc::getFromLibraryPin() const {
	return data->from;
} // end method

// -----------------------------------------------------------------------------

inline
LibraryPin
LibraryArc::getToLibraryPin() const {
	return data->to;
} // end method

// -----------------------------------------------------------------------------

inline
int
LibraryArc::getIndex() const {
	return data->index;
} // end method

} // end namespace
