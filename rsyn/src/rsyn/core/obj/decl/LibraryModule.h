// 文件功能：声明库模块代理，提供模块库对象的访问接口。
// 【零基础阅读】本文件是库模块代理的接口层，将底层资料包装成可用对象。
// class 定义类型，构造函数定义怎样把已有数据关联到代理。
// 名字里的 Library 指可复用模板，与实际模块实例的层次成员要区分。
// data 同名文件存资料，impl 同名文件提供具体访问实现。
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
// data 为 LibraryModuleData，design 是它关联的设计代理；空代理不表示自动创建的新设计。
// 模块模板信息沿设计关联查，不应假设此占位类型另存一份完整成员表。

//! @brief A proxy class representing a library module
//! @note  This is not used yet.
class LibraryModule : public Proxy<LibraryModuleData> {
	
RSYN_FRIEND_OF_GENERIC_LIST_COLLECTION;

friend class RawPointer;
friend class Design;

private:
	LibraryModule(LibraryModuleData * data) : Proxy(data) {}

public:

	//! @brief Default constructor.
	LibraryModule() {}

	//! @brief Assignment constructor to allow null values.
	LibraryModule(std::nullptr_t) {}	

	//! @brief Returns the design in which this library module was created.
	Design getDesign();

	//! @brief Returns the design in which this library module was created.
	const Design getDesign() const;	
	
}; // end class
	
} // end namespace
