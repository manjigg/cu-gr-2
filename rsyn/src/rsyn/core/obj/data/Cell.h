// 文件功能：保留 CellData 占位类型；当前结构为空，实际单元公共数据由 InstanceData 保存。
// 【零基础阅读】Cell 是某个库单元放到设计中的实例，类似同型号的一个实际零件。
// 当前 CellData 不保存独立关联；实际单元借 InstanceData 关联库模板，模板可共享。
// struct/class 内的字段是对象记住的资料，声明字段不等于现在执行布线。
// 使用方通过 Cell 代理查询，具体代理接口和实现分布在 decl/impl 同名文件。
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

struct CellData {
	// 当前没有独立成员变量或构造逻辑。不要从文件名推断这里已有 pins/库型号等字段。
	
}; // end struct

} // end namespace
