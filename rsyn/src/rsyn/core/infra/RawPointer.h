// 文件功能：集中提供网表代理与底层数据指针之间的转换入口，供内部基础设施使用。
// 【零基础阅读】代理像访问钥匙，底层指针是实际资料的地址；此处集中做两者转换。
// 地址不是芯片坐标，转换也不表示复制/重新创建实例。
// 内部基础设施需要直接数据时使用这条桥梁，普通调用方优先用公开查询接口。
/* Copyright 2014-2018 Rsyn
 * 原始指针是档案地址，代理是访问句柄；转换不改变对象 id，也不会产生新的物理实例。
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
 
#ifndef RSYN_RAW_POINTER_H
#define RSYN_RAW_POINTER_H

#include <exception>
#include "rsyn/core/Rsyn.h"

namespace Rsyn {

//! @brief Allows Rsyn objects to be casted as raw pointer (i.e. void *).
class RawPointer {
private:
	void * pointer = nullptr;

public:

	RawPointer(void * data) : pointer(data) {}

	RawPointer(Rsyn::Net obj)         : pointer(static_cast<void *>(obj.getData())) {}
	RawPointer(Rsyn::Pin obj)         : pointer(static_cast<void *>(obj.getData())) {}
	RawPointer(Rsyn::Arc obj)         : pointer(static_cast<void *>(obj.getData())) {}
	RawPointer(Rsyn::Instance obj)    : pointer(static_cast<void *>(obj.getData())) {}
	RawPointer(Rsyn::LibraryPin obj)  : pointer(static_cast<void *>(obj.getData())) {}
	RawPointer(Rsyn::LibraryArc obj)  : pointer(static_cast<void *>(obj.getData())) {}
	RawPointer(Rsyn::LibraryCell obj) : pointer(static_cast<void *>(obj.getData())) {}

	Rsyn::Net         asNet        () { return static_cast<NetData *        >(pointer); }
	Rsyn::Pin         asPin        () { return static_cast<PinData *        >(pointer); }
	Rsyn::Arc         asArc        () { return static_cast<ArcData *        >(pointer); }
	Rsyn::Instance    asInstance   () { return static_cast<InstanceData *   >(pointer); }
	Rsyn::LibraryPin  asLibraryPin () { return static_cast<LibraryPinData * >(pointer); }
	Rsyn::LibraryArc  asLibraryArc () { return static_cast<LibraryArcData * >(pointer); }
	Rsyn::LibraryCell asLibraryCell() { return static_cast<LibraryCellData *>(pointer); }

	operator void *() { return pointer; }
	operator const void *() const { return pointer; }

}; // end class

} // end namespace

#endif /* RAWPOINTER_H */
