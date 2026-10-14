// 文件功能：声明逻辑库代理的库单元查询与集合访问接口。
// 【零基础阅读】本文件提供型号目录的查询入口，查“有哪些型号”而非“有哪些实例”。
// Library 基于 Proxy 使用底层数据，复制句柄通常仍查看同一个库。
// 方法声明里的返回类型说明拿到单个对象还是一个可遍历集合。
// 与 data/Library.h 和 impl/Library.h 配合读，分别理解存储与访问过程。
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
// data 是 LibraryData，其中 designData 指向拥有库集合的设计；库成员不直接存于空占位类型。
// 名称查询取已有型号代理，返回范围可 for 遍历；接口列表不是运行顺序。

// -----------------------------------------------------------------------------

//! @brief A proxy class representing the library (or set of libraries).
//! @note  For now this class is kinda dummy and uses the design to do
//! everything. It should gain importance when we separate more clearly design
//! and library data.
class Library : public Proxy<LibraryData> {
friend class Session;
public:
	Library() {}

	//! @brief Returns an iterable collection of all library cells in the
	//!        design.
	//! @todo  This should be removed once we concentrate everything library
	//!        related in Rsyn::Library.
	Range<ListCollection<LibraryCellData, LibraryCell>>
	allLibraryCells();

private:

	Library(LibraryData * data) : Proxy(data) {}
}; // end class

// -----------------------------------------------------------------------------

} // end method
