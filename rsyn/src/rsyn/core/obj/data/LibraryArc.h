// 文件功能：保存库单元内部的引脚弧关联，为实例化逻辑弧提供模板。
// 【零基础阅读】库弧记录一种单元型号里哪些库引脚存在方向性关联。
// 实例化后可在实际单元的引脚间形成 Arc，所以它是关联模板而不是已布的线。
// 数据成员存端点/归属信息，代理方法从这些数据返回对应库对象。
// 库对象名字里的 Library 表示“型号级”，不表示已经放在芯片某个位置。
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
// 【字段】index 所属型号内弧下标，区别于继承 id；lcell 型号，from/to 起/终库引脚。
// 库弧端点不是某实例的放置坐标，实际实例弧另存 ArcData。

struct LibraryArcData : ObjectData {
	Design design;
	int index; // TODO: index may be confusing as objects have a field called id 
	
	LibraryCell lcell;
	LibraryPin from;
	LibraryPin to;

	LibraryArcData() : 
		design(nullptr),
		index(-1), 
		lcell(nullptr),
		from(nullptr), 
		to(nullptr) {
	} // end class
}; // end struct

} // end namespace
