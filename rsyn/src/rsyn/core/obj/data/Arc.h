// 文件功能：保存逻辑弧的类型、起终引脚及所属线网/库弧，供 Arc 代理访问。
// 【零基础阅读】逻辑弧是“一个引脚到另一个引脚的关联”，并不是物理导线形状。
// from/to 保存两端引脚；type 区分单元内部弧和线网弧，决定 union 中哪个成员有意义。
// union 是共享同一存储区的多个表示；此处不能把每个成员都当成同时有效。
// 本文件放真实字段，decl/Arc.h 提供访问接口，impl/Arc.h 从字段取值。
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
// 【字段】type 弧类别，from/to 起/终逻辑端点；extra union 空占位。
// libraryArcData 单元内部弧模板地址，netData 线网弧所属网地址，按 type 只选一种有效归属。
		
struct ArcData : ObjectData {
	ArcType type : 3;
	Pin from;
	Pin to;
	
	union {
		// Place holder for initialization...
		void * extra;
		
		// Meaningful only when this is arc belongs to a cell.
		LibraryArcData * libraryArcData;
		
		// Meaningful only when this is arc belongs to a net.
		NetData * netData;
	}; // end union

	ArcData() : 
		type(UNKNOWN_ARC_TYPE),
		extra(nullptr),
		from(nullptr), 
		to(nullptr) {
	} // end constructor
}; // end struct

} // end namespace
