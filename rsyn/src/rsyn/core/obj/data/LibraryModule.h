// 文件功能：保存库模块的底层信息，支撑层次模块的库级表示。
// 【零基础阅读】模块可把一组内部结构封装成可复用的层次对象，库模块是其库级表示。
// 本文件定义底层资料，代理声明在 decl/LibraryModule.h，方法在 impl 同名文件。
// 继承关系若出现，表示沿用公共对象字段，不是把父类算法执行一次。
// 先认清 LibraryModule 与实际 Module 的区别，再追导入时如何建立关联。
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

struct LibraryModuleData : ObjectData {
	// design：关联的逻辑设计代理，默认空；id 继承自公共对象。此处没有额外成员集合。
	Design design;
	
	LibraryModuleData() :
		design(nullptr) {
	} // end constructor
}; // end struct

} // end namespace
