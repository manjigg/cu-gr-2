// 文件功能：实现过孔几何的边界和工艺层读取。
// 【零基础阅读】读取过孔的一块局部形状，返回所属层和矩形边界。
// 放到芯片位置时要做坐标平移，局部 (0,0) 不一定是芯片原点。
// 多块形状可能构成整只过孔，因此调用方会对各层几何做循环。
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
 * File:   PhysicalViaGeometry.h
 * Author: jucemar
 *
 * Created on November 12, 2018, 10:14 PM
 */

namespace Rsyn {
// clsBounds 局部 DBU 边界，clsMaskNumber 光罩编号；平移到过孔实例位置不在此短查询里完成。

inline const Bounds & PhysicalViaGeometry::getBounds() const {
	return data->clsBounds;
} // end method 

// -----------------------------------------------------------------------------

inline int PhysicalViaGeometry::getMaskNumber() const {
	return data->clsMaskNumber;
} // end method 

// -----------------------------------------------------------------------------

} // end namespace 
