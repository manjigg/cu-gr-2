// 文件功能：定义 Rsyn 网表对象共用的底层身份及管理数据。
// 【零基础阅读】公共对象数据提供“我是谁”的基础，而派生对象增加自己的专门资料。
// 类比每张档案先有编号，再按单元/引脚/线网类别增加不同栏目。
// 指针/引用让关联对象共享同一档案，不能把地址数字误当成几何坐标。
// 这是许多网表对象的基础，阅读时先分清身份信息与连接/物理信息。
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
		
struct ObjectData {
	// id：集合管理的稳定对象身份编号，不是内存地址、层编号或坐标。
	Index id;
}; // end struct
	
} // end namespace
