// 文件功能：实现 Arc 查询方法，从底层数据返回端点、所属对象和弧名称。
// 【零基础阅读】这里实现上层菜单：从 data 或端点引脚取得弧类型/归属/名称。
// data->type 是通过底层地址访问字段；返回代理不意味着重新创建物理对象。
// 按弧类别返回 Net 或 Instance，不适用的归属可返回空代理。
// inline 允许这份方法定义被头文件引入多个编译单元，不保证机器码一定被展开。
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
// 【逻辑】先按 data->type 区分单元/网弧，再从 libraryArcData/netData 或 from/to 取所属对象。
// data 是地址，返回值是代理/文字；没有适用归属时为空，不是在此创建端点或导线。

inline
Design 
Arc::getDesign() {
	return getFromPin().getDesign();
} // end method

// -----------------------------------------------------------------------------

inline
const Design 
Arc::getDesign() const {
	return getFromPin().getDesign();
} // end method

// -----------------------------------------------------------------------------

inline
ArcType 
Arc::getType() const {
	return data->type;
} // end method

// -----------------------------------------------------------------------------

inline
Instance 
Arc::getInstance() const {
	return data->type == INSTANCE_ARC? data->from.getInstance() : nullptr;
} // end method

// -----------------------------------------------------------------------------

inline
Net 
Arc::getNet() const {
	return data->type == NET_ARC? data->netData : nullptr;
} // end method
	
// -----------------------------------------------------------------------------
	
inline
LibraryArc
Arc::getLibraryArc() const {
	return getType() == INSTANCE_ARC? data->libraryArcData : nullptr;
} // end method

// -----------------------------------------------------------------------------

inline
LibraryCell
Arc::getLibraryCell() const {
	Rsyn::LibraryArc larc = getLibraryArc();
	return larc? larc.getLibraryCell() : nullptr;
} // end method

// -----------------------------------------------------------------------------

inline
Pin 
Arc::getFromPin() const {
	return data->from;
} // end method

// -----------------------------------------------------------------------------	
	
inline
Pin 
Arc::getToPin() const {
	return data->to;
} // end method

// -----------------------------------------------------------------------------

inline
std::string
Arc::getName() const {
	return getFromName() + "->" + getToName();
} // end method

// -----------------------------------------------------------------------------

inline
std::string
Arc::getFullName() const {
	return getType() == NET_ARC?
		(getFromPin().getFullName() + "->" + getToPin().getFullName()) :
		(getInstance().getName() + ":" + getName());
} // end method

// -----------------------------------------------------------------------------

inline
const std::string
Arc::getFromName() const {
	return getFromPin().getName();
} // end method

// -----------------------------------------------------------------------------

inline
const std::string
Arc::getToName() const {
	return getToPin().getName();
} // end method

// -----------------------------------------------------------------------------

inline
Net
Arc::getFromNet() const {
	return getFromPin().getNet();
} // end method

// -----------------------------------------------------------------------------

inline
Net
Arc::getToNet() const {
	return getToPin().getNet();
} // end method

// -----------------------------------------------------------------------------

inline
int
Arc::getIndex() const {
	return getLibraryArc()? getLibraryArc().getIndex() : -1;
} // end method

} // end namespace
