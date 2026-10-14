// 文件功能：实现 GCELLGRID 方向、位置、数量和步长读取。
// 【零基础阅读】getDirection/getLocation/getNumTracks/getStep 读取四项网格定义。
// data->clsStep 等是字段访问，没有在查询时重新从 DEF 文本解析。
// 外部按起点+i×步长展开边界，数量减一才是相邻边界间的格子数。
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
 * File:   PhysicalGCell.h
 * Author: jucemar
 *
 * Created on January 22, 2018, 9:23 PM
 */

namespace Rsyn {
// 每个 get 方法分别返回 clsDirection/Location/NumTracks/Step；前后两数量/步长不能互换。
//! @brief Returns GCell direction

inline Rsyn::PhysicalGCellDirection PhysicalGCell::getDirection() const {
	return data->clsDirection;
} // end method 

// -----------------------------------------------------------------------------

//! @brief If direction is vertical, then numTracks means the number of columns
//! If direction is Horizontal, then numTracks means the number of rows

inline int PhysicalGCell::getNumTracks() const {
	return data->clsNumTracks;
} // end method 

// -----------------------------------------------------------------------------
//! @brief If direction is Horizontal, then location is Y value.
//! If direction is vertical, then location is X value.

inline DBU PhysicalGCell::getLocation() const {
	return data->clsLocation;
} // end method 

// -----------------------------------------------------------------------------

//! @brief Returns the spacing between GCell tracks. 

inline DBU PhysicalGCell::getStep() const {
	return data->clsStep;
} // end method 

// -----------------------------------------------------------------------------

} // end namespace 
