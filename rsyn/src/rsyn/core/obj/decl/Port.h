// 文件功能：声明外部端口代理的方向、内外引脚和所属模块查询接口。
// 【零基础阅读】Port 是模块边界的接线口，接口区分内部与外部引脚。
// 方向描述信号进入还是离开模块；物理朝向另由 PhysicalPort 保存。
// Proxy 句柄关联 PortData；返回的引脚还能继续查询所连的 Net。
// 看 impl/Port.h 理解访问路径，而不是把声明列表当成连续执行代码。
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
// data 实际端口的公共实例记录；内外引脚按模块边界角色返回，不是两个物理金属层。
// PortData 当前为空占位，因此理解 get* 要继续跟 InstanceData 而非寻找不存在的字段。

//! @brief A proxy class representing a netlist port.
class Port : public Instance {
	
RSYN_FRIEND_OF_GENERIC_LIST_COLLECTION;

friend class RawPointer;
friend class Design;
friend class Instance;

private:	
	Port(InstanceData * data) : Instance(data) {}
public:

	//! @brief Default constructor.
	Port() : Instance(nullptr) {}

	//! @brief Assignment constructor to allow null values.
	Port(std::nullptr_t) : Instance(nullptr) {}	

	//! @brief Returns the pin inside the current level of hierarchy (module).
	//! @note  This pin belongs to the port instance.
	Pin getInnerPin() const;

	//! @brief Returns the pin in the parent hierarchy (module).
	//! @note  This pin does not belongs to the port instance. It belongs to the
	//!        parent module instance. That is the outer pin is only virtually
	//!        associated to a port.
	Pin getOuterPin() const; 

	//! @brief Returns the inner/outer pin related to an outer/inner pin.
	//!        If the outer pin is passed, returns the inner pin.
	//!        If the inner pin is passed, returns the outer pin.
	//!        If the pin does not match both inner and outer pins, returns
	//!        null.
	//! @note  The outer pin may be null if the inner pin belongs to
	//!        a port in the top level hierarchy.
	Pin getOtherPin(Rsyn::Pin pin) const;

	//! @brief Returns the direction of this port.
	//! @note  The direction of a port matches the direction of its inner pin.
	Direction getDirection() const;
}; // end class
	
} // end namespace
