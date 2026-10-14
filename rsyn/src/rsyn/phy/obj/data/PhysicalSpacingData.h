// 文件功能：保存工艺间距条目的参数和关联。
// 【零基础阅读】Spacing 记录金属对象之间应留多少间隙的工艺描述。
// 留白是为了满足制造/电气规则，不能理解为某条线网已经消耗的容量。
// 本文件保存间距条目，查询/规则解释在对应代理及调用方完成。
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
 * File:   PhysicalSpacingData.h
 * Author: jucemar
 *
 * Created on 15 de Setembro de 2016, 18:55
 */

#ifndef PHYSICALDESIGN_PHYSICALSPACINGDATA_H
#define PHYSICALDESIGN_PHYSICALSPACINGDATA_H

namespace Rsyn {
// clsLayer1/clsLayer2 相关两层，clsDistance 要求间隙 DBU，不是从当前几何测出的实际距离。

class PhysicalSpacingData : public PhysicalObject {
public:
	PhysicalLayer clsLayer1;
	PhysicalLayer clsLayer2;
	DBU clsDistance = 0;
	PhysicalSpacingData() = default;
}; // end class 

} // end namespace 


#endif /* PHYSICALDESIGN_PHYSICALSPACINGDATA_H */
