// 文件功能：定义 LibraryData 的所属设计地址；具体库集合和名字索引在 DesignData 中。
// 【零基础阅读】Library 是可用零件型号的目录，区别于已经放置的实例名单。
// 库单元是模板，设计实例引用模板；很多实例可以共用同一个库单元。
// 本文件只存所属设计地址，目录实际在 DesignData；查询/遍历接口在 decl/impl 同名文件。
// 阅读集合成员时可把容器理解为列表或字典，名字查找与按顺序遍历是两种操作。
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

struct LibraryData : ObjectData {
	// designData：拥有该库资料的设计数据地址，初始为空；id 从 ObjectData 继承。
	DesignData *designData = nullptr;
}; // end struct

} // end namespace
