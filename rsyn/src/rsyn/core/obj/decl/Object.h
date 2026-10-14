// 文件功能：声明网表对象的公共身份、类型及基础访问接口。
// 【零基础阅读】公共对象代理提供所有网表对象共有的访问基础。
// 派生类型继承这份接口，再增加单元/引脚/线网自己的问题菜单。
// 对象编号用于身份和索引，不是芯片上的距离或层编号。
// 方法体可能在本文件内联，也可能在 impl/Object.h，按实际花括号定位。
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
// data 底层记录地址，id 稳定身份编号；对象比较应看代理定义，不能把地址当几何坐标。
// 这里提供公共访问约定，专门字段需进入对应实例/线网/引脚对象。

// TODO create a base class for everyone...
	
} // end namespace
