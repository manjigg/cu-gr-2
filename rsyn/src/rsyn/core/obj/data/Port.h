// 文件功能：保留 PortData 占位类型；当前结构为空，端口公共实例资料在 InstanceData 中。
// 【零基础阅读】Port 是模块边界的接线口，将内部信号与外部连接对应起来。
// 它和普通单元内部引脚用途不同，所以有专门的代理及内外引脚关系。
// 当前 PortData 为空，逻辑关联借实例资料表达；位置/朝向/矩形在 PhysicalPort 里。
// 阅读时不要把模块端口、库单元引脚和实际单元引脚当成同一种对象。
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

struct PortData {
	// 这里没有可逐项解释的成员；内外引脚查询请读 Port 代理和 InstanceData 的关系。
	
}; // end struct

} // end namespace
