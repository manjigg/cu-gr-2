// 文件功能：保存物理区域的名称、类型和边界约束。
// 【零基础阅读】Region 是布局约束区域，其边界可由多个矩形描述。
// 名称便于对象引用它，类型说明约束类别，边界说明空间范围。
// 存下区域并不自动改变路线；使用方必须查询并决定如何应用约束。
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
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   PhysicalRegionData.h
 * Author: jucemar
 *
 * Created on 09 de Abril de 2017, 14:55
 */

#ifndef PHYSICALDESIGN_PHYSICALREGIONDATA_H
#define PHYSICALDESIGN_PHYSICALREGIONDATA_H


namespace Rsyn {
// clsBounds[i] 区域第 i 块 DBU 矩形，多块共同描述区域，包围盒不保证其内全部都是有效区域。

class PhysicalRegionData : public PhysicalObject {
public:

	std::string clsName = Rsyn::getPhysicalInvalidName();
	RegionType clsType = RegionType::INVALID; // FENCE or GUIDE
	std::vector<Bounds> clsBounds;
	PhysicalRegionData() = default;
}; // end class  

} // end namespace 

#endif /* PHYSICALDESIGN_PHYSICALREGIONDATA_H */
