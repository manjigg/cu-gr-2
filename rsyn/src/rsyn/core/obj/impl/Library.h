// 文件功能：实现逻辑库的名称和库单元集合查询。
// 【零基础阅读】查询名字和库成员时，从 LibraryData 保存的目录返回对应结果。
// 这像查型号手册，不涉及具体实例的放置位置或路由资源需求。
// data->... 访问底层记录；迭代范围提供已有记录的顺序访问。
// 配合 decl 的返回类型看，分清拿到单个库单元还是库单元集合。
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
// data->designData 是库所在设计资料；真正 libraryCells 等集合在该 DesignData 上。
// 这里通过关联访问集合/名称索引，不能误解成 LibraryData 自己还保存另一套库目录。

inline
Range<ListCollection<LibraryCellData, LibraryCell>>
Library::allLibraryCells() {
	return Rsyn::Design(data->designData).allLibraryCells(false);
} // end method

} // end namespace
