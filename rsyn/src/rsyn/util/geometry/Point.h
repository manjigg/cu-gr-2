// 文件功能：定义通用几何点类型，供矩形、多边形和布线路径表示使用。
// 【零基础阅读】Point 是几何上的一个位置，由两个坐标描述，不表示有宽度的导线。
// 矩形/多边形用点组织形状，路径用点组织中心线，用途由持有者决定。
// 先读构造和坐标查询，再看算术/转换接口，注意它使用的具体数值类型。
/* Copyright 2014-2018 Rsyn
 * 点的两分量为几何位置，另一点/位移参数为计算对象；点无宽高，不能替代导线面积。
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

#ifndef RSYN_GEOMETRY_POINT_H
#define RSYN_GEOMETRY_POINT_H

#include <boost/geometry.hpp>
#include <boost/geometry/geometries/register/point.hpp>
#include <boost/geometry/geometries/geometries.hpp>

#include "rsyn/util/dbu.h"

namespace Rsyn {

class Point {
public:

	Point() {}
	Point(const DBU x, const DBU y) : clsX(x), clsY(y) {}
	Point(const DBUxy p) : clsX(p.x), clsY(p.y) {}

	DBU getX() const {return clsX;}
	DBU getY() const {return clsY;}

	void setX(const DBU x) {clsX = x;}
	void setY(const DBU y) {clsY = y;}

	//! @brief Translates this point by (dx, dy).
	void translate(const DBU dx, const DBU dy) {
		clsX += dx;
		clsY += dy;
	} // end method

	//! @brief Translates this point by displacement.
	void translate(const DBUxy displacement) {
		translate(displacement.x, displacement.y);
	} // end method

	//! @brief Returns copy of this point translated by (dx, dy).
	Point translated(const DBU dx, const DBU dy) const {
		Point point = *this;
		point.translate(dx, dy);
		return point;
	} // end method

	//! @brief Returns copy of this point translated by displacement.
	Point translated(const DBUxy displacement) const {
		return translated(displacement.x, displacement.y);
	} // end method

	operator DBUxy() const {return DBUxy(clsX, clsY);}

private:

	DBU clsX = 0;
	DBU clsY = 0;

}; // end class

} // end namespace

BOOST_GEOMETRY_REGISTER_POINT_2D_GET_SET(Rsyn::Point, DBU, 
		boost::geometry::cs::cartesian, getX, getY, setX, setY);

#endif
