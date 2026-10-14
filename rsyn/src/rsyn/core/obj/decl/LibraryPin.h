// 文件功能：声明库引脚的方向、用途、名称及所属库对象查询接口。
// 【零基础阅读】此代理让调用方查询模板接线端名称、输入/输出方向及信号用途。
// 方向枚举像 IN/OUT 是类别，不能理解成 x/y 方向或金属布线方向。
// getLibraryCell 一类关联查询可回到所属型号，进一步遍历它的其他引脚。
// 方法末尾 const 说明只读；这里的方法列表尚不是执行顺序。
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
// data 为 LibraryPinData；index 型号内引脚下标，name 引脚名，lcell 型号，direction 信号方向。
// pinUse 表示电源/信号用途；名字/方向/用途查询与几何矩形查询是不同模块。

//! @brief A proxy class representing a library arc.
class LibraryPin : public Proxy<LibraryPinData> {
	
RSYN_FRIEND_OF_GENERIC_LIST_COLLECTION;

friend class RawPointer;
friend class Design;
friend class Cell;
friend class Sandbox;
friend class SandboxCell;

private:
	LibraryPin(LibraryPinData * data) : Proxy(data) {}
	
public:

	//! @brief Default constructor.
	LibraryPin() {}

	//! @brief Assignment constructor to allow null values.
	LibraryPin(std::nullptr_t) {}	

	//! @brief Returns the design in which this library pin was created.
	Design getDesign();

	//! @brief Returns the design in which this library pin was created.
	const Design getDesign() const;	

	//! @brief Returns the name of this library pin.
	const std::string &getName() const;

	//! @brief Returns the names of this library cell and pin .
	std::string getHierarchicalName(const std::string & separator = ":") const;
	
	//! @brief Returns the direction name of this library pin.
	const std::string &getDirectionName() const;

	//! @brief Returns the library cell name associated to this library pin.
	const std::string &getLibraryCellName() const;

	//! @brief Returns the direction of this library pin.
	Direction getDirection() const;

	//! @brief Returns the library cell of this library pin.
	LibraryCell getLibraryCell() const;

	//! @brief Returns the index of this library pin inside its library cell.
	int getIndex() const;

	//! @brief Returns true if this library pin is an input and false otherwise.
	bool isInput() const;

	//! @brief Returns true if this library pin is an output and false
	//!        otherwise.
	bool isOutput() const;

	//! @brief Returns true if this library pin is a bidirectional and false
	//!        otherwise.
	bool isBidirectional() const;
	
	//! @Author Mateus
	//! @brief Returns the usage of the pin
	Use getUse() const;
	
	//! @Author Mateus
	//! @brief Returns true if the pin is used for power or ground or false
	//!        otherwise.
	bool isPowerOrGround() const;
}; // end class
	
} // end namespace
