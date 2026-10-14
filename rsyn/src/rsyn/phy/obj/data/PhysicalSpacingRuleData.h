// 文件功能：保存物理间距规则的约束参数。
// 【零基础阅读】间距可能随线宽、并行长度等条件改变，所以规则有参数而非单个常数。
// 数据成员记条件和要求，使用方查询匹配条件后才知道应保留多大间隙。
// 这是工艺资料存储，CUGR 对其使用是通过层对象提取所需部分。
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
 * File:   PhysicalSpacingRuleData.h
 * Author: jucemar
 *
 * Created on 17 de dezembro de 2017, 14:10
 */

#ifndef PHYSICALDESIGN_PHYSICALSPACINGRULEDATA_H
#define PHYSICALDESIGN_PHYSICALSPACINGRULEDATA_H

namespace Rsyn {
// clsSpacing 间隙，clsEOL 线端相关宽度参数，clsEOLWithin 规则作用距离，单位均 DBU。

class PhysicalSpacingRuleData {
public:
	DBU clsSpacing = 0;
	DBU clsEOL = 0;
	DBU clsEOLWithin = 0;
	PhysicalSpacingRuleData() = default;
}; // end class 

} // end namespace 

#endif /* PHYSICALDESIGN_PHYSICALSPACINGRULEDATA_H */
