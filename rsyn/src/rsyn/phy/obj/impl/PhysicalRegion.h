// 文件功能：实现物理区域名称、类型和边界集合读取。
// 【零基础阅读】返回区域名字、类别和边界集合，供约束使用方判断空间范围。
// 一个区域可以有多个矩形，不能仅按第一个矩形就代表整个区域。
// 数据查询和算法应用约束是两步，这里不负责改变信号路线。
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
 * File:   PhysicalRegion.h
 * Author: jucemar
 *
 * Created on 09 de Abril de 2017, 14:55
 */
namespace Rsyn {
// clsBounds[i] 第 i 个区域矩形，name 名称键，type 区域类别；不是候选路线编号。

// -----------------------------------------------------------------------------

inline const std::string & PhysicalRegion::getName() const {
	return data->clsName;
} // end method 

// -----------------------------------------------------------------------------

inline RegionType PhysicalRegion::getType() const {
	return data->clsType;
} // end method 

// -----------------------------------------------------------------------------

inline const std::vector<Bounds> & PhysicalRegion::allBounds() const {
	return data->clsBounds;
} // end method 

// -----------------------------------------------------------------------------

inline std::size_t PhysicalRegion::getNumBounds() const {
	return data->clsBounds.size();
} // end method 

// -----------------------------------------------------------------------------

} // end namespace
