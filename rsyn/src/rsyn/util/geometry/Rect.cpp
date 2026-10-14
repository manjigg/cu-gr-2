// 文件功能：实现矩形几何对象的构造和边界/尺寸操作。
// 【零基础阅读】把传入的矩形参数组织成存储并实现边界/尺寸访问。
// 位置改变与尺寸改变是不同操作，理解方法时先看它更新哪些量。
// 这是已有形状的表达，不进行拥塞代价计算或新增线网连接。
/* Copyright 2014-2018 Rsyn
 * 构造参数装成矩形位置/尺寸，平移改位置不改跨度，边界查询由位置与宽高组合取得。
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

#include "Rect.h"
#include "Polygon.h"

namespace Rsyn {

// -----------------------------------------------------------------------------

bool
Rect::overlaps(const Polygon &polygon) const {
	return polygon.overlaps(*this);
} // end method

// -----------------------------------------------------------------------------

Polygon
Rect::toPolygon() const {
	Polygon polygon;
	polygon.addPoint(getLower().getX(), getLower().getY());
	polygon.addPoint(getUpper().getX(), getLower().getY());
	polygon.addPoint(getUpper().getX(), getUpper().getY());
	polygon.addPoint(getLower().getX(), getUpper().getY());
	return polygon;
} // end method

} // end namespace
