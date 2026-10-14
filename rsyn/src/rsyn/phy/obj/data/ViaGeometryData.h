// 文件功能：保存单个过孔形状的矩形边界和所属层。
// 【零基础阅读】一个过孔可以有多块形状，本记录只描述其中一块及它所在的层。
// 矩形边界是局部几何，实例过孔放到路径点后还需要加位置位移。
// 几何记录与整只过孔不是同一对象，遍历全部形状才能取得完整占用。
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
 * File:   ViaGeometryData.h
 * Author: jucemar
 *
 * Created on November 12, 2018, 5:44 PM
 */

#ifndef RSYN_DATABASE_VIAGEOMETRYDATA_H
#define RSYN_DATABASE_VIAGEOMETRYDATA_H

namespace Rsyn {
// clsBounds 局部 DBU 形状，clsMaskNumber 光罩编号，-1 表示未指定而不是“负一金属层”。
class ViaGeometryData : public PhysicalObject {
public:
	Bounds clsBounds;
	int clsMaskNumber = -1;
	// Polygon
}; // end class 

} // end namespace 
#endif /* RSYN_DATABASE_VIAGEOMETRYDATA_H */
