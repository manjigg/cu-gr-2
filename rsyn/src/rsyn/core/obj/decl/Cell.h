// 文件功能：声明 Cell 单元代理的库关联、引脚查询和单元属性接口。
// 【零基础阅读】这里列出“一个实际单元能被怎样查询”的接口。
// Cell 代理指向 CellData；库关联告诉你它是什么型号，引脚接口告诉你有哪些端点。
// 声明只说方法名、输入和返回类型，分号结尾的方法要继续到 impl/Cell.h 找方法体。
// public 是调用方可用的菜单，friend 为特定内部管理者开放访问权限。
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
// 【变量读法】data 是实际 InstanceData 地址；库关联经 lcell，pin 是当前实际端点。
// 方法末尾 const 为只读查询；index/name 参数分别按型号引脚编号/名字查，不是物理坐标。

//! @brief A proxy class representing a netlist cell.
class Cell : public Instance {
friend class RawPointer;
friend class Design;
friend class Instance;
private:	
	Cell(InstanceData * data) : Instance(data) {}
public:

	//! @brief Default constructor.
	Cell() : Instance(nullptr) {}

	//! @brief Assignment constructor to allow null values.
	Cell(std::nullptr_t) : Instance(nullptr) {}

	//! @brief Returns the name of the library cell associated to this cell.
	const std::string &getLibraryCellName() const;

	//! @brief Returns the library cell associated to this cell.
	LibraryCell getLibraryCell() const;

	//! @brief Returns the respective pin in this cell associated to a library
	//!        pin if any.
	Pin getPinByLibraryPin(LibraryPin lpin) const;

	//! @brief Changes the library cell of this cell. The new library cell must
	//!        have the same interface (i.e. pin names and directions) as the
	//!        old one.
	void remap(LibraryCell libraryCell);

	//! @brief Changes the library cell of this cell. The new library cell must
	//!        have the same interface (i.e. pin names and directions) as the
	//!        old one.
	void remap(const std::string &libraryCellName);
	
}; // end class
	
} // end namespace
