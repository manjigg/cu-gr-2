// 文件功能：实现物理实例的类型、位置、边界及关联代理读取。
// 【零基础阅读】读取实际物理实例的通用位置、类型及边界，并提供关联访问。
// 类型决定能否转成 PhysicalCell/Port 等专门对象，位置和尺寸要分别理解。
// 代理返回共享数据视图，不是创建一个额外实例或重新放置单元。
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
 
/* 
 * File:   PhysicalInstance.h
 * Author: jucemar
 *
 * Created on 4 de Outubro de 2016, 19:37
 */


namespace Rsyn {
// clsInstance 是逻辑关联，clsInitialPos 初始 DBU 点，clsPlaced/Block/Port 为类型/状态标志。

// -----------------------------------------------------------------------------

inline Rsyn::Instance PhysicalInstance::getInstance() const {
	return data->clsInstance;
} // end method 

// -----------------------------------------------------------------------------

inline const std::string &PhysicalInstance::getName() const {
	return data? getInstance().getName() : NullName;
} // end method

// -----------------------------------------------------------------------------

inline DBU PhysicalInstance::getArea() const {
	return getInstance().getArea();
} // end method 

// -----------------------------------------------------------------------------

inline DBUxy PhysicalInstance::getCenter() const {
	return getInstance().getCenter();
} // end method 

// -----------------------------------------------------------------------------

inline DBU PhysicalInstance::getCenter(const Dimension dim) const {
	return getInstance().getCenter(dim);
} // end method 

// -----------------------------------------------------------------------------

inline DBU PhysicalInstance::getX() const {
	return getInstance().getX();
} // end method

// -----------------------------------------------------------------------------

inline DBU PhysicalInstance::getY() const {
	return getInstance().getY();
} // end method

// -----------------------------------------------------------------------------

inline DBU PhysicalInstance::getHeight() const {
	return getInstance().getHeight();
} // end method 

// -----------------------------------------------------------------------------

inline DBU PhysicalInstance::getWidth() const {
	return getInstance().getWidth();
} // end method 

// -----------------------------------------------------------------------------

inline DBU PhysicalInstance::getSize(const Dimension dimension) const {
	return getInstance().getSize(dimension);
} // end method 

// -----------------------------------------------------------------------------

inline DBUxy PhysicalInstance::getSize() const {
	return getInstance().getSize();
} // end method

// -----------------------------------------------------------------------------

inline DBUxy PhysicalInstance::getPosition() const {
	return getInstance().getPosition();
} // end method 

// -----------------------------------------------------------------------------

inline DBU PhysicalInstance::getPosition(const Dimension dim) const {
	return getInstance().getPosition(dim);
} // end method 

// -----------------------------------------------------------------------------

inline DBUxy PhysicalInstance::getCoordinate(const Boundary bound) const {
	return getInstance().getCoordinate(bound);
} // end method 

// -----------------------------------------------------------------------------

inline DBU PhysicalInstance::getCoordinate(const Boundary bound, const Dimension dim) const {
	return getInstance().getCoordinate(bound, dim);
} // end method 

// -----------------------------------------------------------------------------

inline const Bounds &PhysicalInstance::getBounds() const {
	return getInstance().getBounds();
} // end method 

// -----------------------------------------------------------------------------

inline PhysicalOrientation PhysicalInstance::getOrientation() const {
	return getInstance().getOrientation();
} // end method

// -----------------------------------------------------------------------------

inline Rsyn::PhysicalCell PhysicalInstance::asPhysicalCell() const {
//#ifdef RSYN_SAFE_MODE
	if (data->clsInstance.getType() != Rsyn::CELL)
		throw SafeModeException("Invalid instance casting. Instance is not a cell.");
//#endif
	return PhysicalCell(data);
} // end method 

// -----------------------------------------------------------------------------

} // end namespace 
