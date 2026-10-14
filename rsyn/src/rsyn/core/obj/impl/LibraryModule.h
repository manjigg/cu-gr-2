// 文件功能：实现库模块代理对底层模块信息的访问。
// 【零基础阅读】此文件实现库模块代理对既有数据的访问。
// :: 表示“某类型的方法”，data 指代理所关联的实际资料。
// 模块库表示提供复用模板语义；实际模块成员的查询请对照 Module 系列。
// 小方法在头文件中定义可供多个源文件使用，不代表程序启动时自动执行它们。
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
// data->design 是关联设计代理，其他模块信息经此设计取得；nullptr 是未关联状态。
// 这里使用的是库模块接口，并没有实际路线容量或候选 DAG 变量。

inline
Design 
LibraryModule::getDesign() {
	return data->design;
} // end method

// -----------------------------------------------------------------------------

inline
const Design 
LibraryModule::getDesign() const {
	return data->design;
} // end method

// -----------------------------------------------------------------------------
	
} // end namespace
