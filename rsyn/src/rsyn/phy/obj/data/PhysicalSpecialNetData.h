// 文件功能：保存特殊线网的 DEF 描述和特殊导线数据。
// 【零基础阅读】特殊线网常用于电源等已有布线，保存 DEF 导线/过孔描述。
// CUGR 不把这些几何当普通信号重新规划，而将其视为固定占用障碍。
// 数据保留路径详情，查询后需转换成各层矩形才能影响容量估计。
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
 * File:   PhysicalSpecialNetData.h
 * Author: jucemar
 *
 * Created on 23 de Maio de 2017, 20:23
 */

#ifndef PHYSICALDESIGN_PHYSICALSPECIALNETDATA_H
#define PHYSICALDESIGN_PHYSICALSPECIALNETDATA_H

namespace Rsyn {
// 特殊网描述保留导线和路径点记录：网名为文字身份，层名查工艺层，线宽/坐标为 DBU 几何。
// 查询后的 wire/segment/point 分别定位路径、段和点，不能与普通 Net 的端点编号表混用。
class PhysicalSpecialNetData : public PhysicalObject {
public:
	DefNetDscp clsNet;
	
	PhysicalSpecialNetData() = default;
}; // end class 

} // end namespace 

#endif /* PHYSICALDESIGN_PHYSICALSPECIALNETDATA_H */
