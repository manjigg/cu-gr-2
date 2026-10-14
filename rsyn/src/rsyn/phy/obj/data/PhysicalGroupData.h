// 文件功能：保存物理分组的成员及约束关联。
// 【零基础阅读】Group 将若干实例视为一组，常用于关联同一物理约束。
// 成员描述“有哪些对象”，区域关联描述“约束在哪里”，二者不同。
// 本文件定义保存格式，分组查询交给 PhysicalGroup 代理。
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
 * File:   PhysicalGroupData.h
 * Author: jucemar
 *
 * Created on 09 de Abril de 2017, 16:45
 */

#ifndef PHYSICALDESIGN_PHYSICALGROUPDATA_H
#define PHYSICALDESIGN_PHYSICALGROUPDATA_H


namespace Rsyn {
// clsPatterns 是成员名匹配模式字符串表，clsRegion 关联约束区域；模式不是实际成员对象。

class PhysicalGroupData : public PhysicalObject {
public:

	std::string clsName = Rsyn::getPhysicalInvalidName();
	std::vector<std::string> clsPatterns; // e.g. "name/*"
	Rsyn::PhysicalRegion clsRegion;
	PhysicalGroupData() = default;
}; // end class  

} // end namespace 

#endif /* PHYSICALDESIGN_PHYSICALGROUPDATA_H */
