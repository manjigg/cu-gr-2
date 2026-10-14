// 文件功能：实现端口的内部/外部引脚、方向和模块访问方法。
// 【零基础阅读】实现边界端口到内部/外部引脚和所属模块的查询。
// 一个端口连接层次内外，内部引脚与外部引脚的角色不应混为同一连接方向。
// 返回代理让调用者继续沿逻辑网表关系查询，不复制整个模块。
// 与 PhysicalPort 的几何方法分开：这里回答信号关系，后者回答位置和形状。
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
// data 为端口对应实例资料；direction 信号进出方向，内部/外部引脚按模块边界规则取得。
// PortData 本身无字段；公开名字/端点信息由共享实例资料支撑。

// -----------------------------------------------------------------------------

inline
Pin 
Port::getInnerPin() const {
	// Ports have just one pin, which is the inner pin...
	return data->pins[0];
} // end method

// -----------------------------------------------------------------------------

inline
Pin 
Port::getOuterPin() const {
	return Pin(data->outerPin);
} // end method

// -----------------------------------------------------------------------------

inline
Pin 
Port::getOtherPin(Pin pin) const {
	Rsyn::Pin inner = getInnerPin();
	Rsyn::Pin outer = getOuterPin();
	if (inner == pin) return outer;
	if (outer == pin) return inner;
	return nullptr;
} // end method

// -----------------------------------------------------------------------------

inline
Direction
Port::getDirection() const {
	return Global::getReverseDirection(getInnerPin().getDirection());
} // end method
	
} // end namespace
