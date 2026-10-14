// 文件功能：实现放置 site 的名称、尺寸及类型读取。
// 【零基础阅读】从 site 模板取得名称和基本座位尺寸，用于解释放置行/单元布局。
// 共享模板可以被许多行引用，返回代理不复制所有放置位置。
// 本文件实现短查询方法，声明在 decl，真实字段在 data。
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
 * File:   PhysicalSite.h
 * Author: jucemar
 *
 * Created on 18 de Setembro de 2016, 08:42
 */

namespace Rsyn {
// clsSize.x/y DBU 宽高，clsSiteClass 类型，clsSymmetry 对称标志；get 查询不实际摆放单元。

inline std::string PhysicalSite::getName() const {
	return data->clsSiteName;
} // end method 

// -----------------------------------------------------------------------------

inline Rsyn::PhysicalSiteClass PhysicalSite::getClass() const {
	return data->clsSiteClass;
} // end method 

// -----------------------------------------------------------------------------

inline DBUxy PhysicalSite::getSize() const {
	return data->clsSize;
} // end method 

// -----------------------------------------------------------------------------

inline DBU PhysicalSite::getWidth() const {
	return data->clsSize[X];
} // end method 

// -----------------------------------------------------------------------------

inline DBU PhysicalSite::getHeight() const {
	return data->clsSize[Y];
} // end method 

// -----------------------------------------------------------------------------

inline DBU PhysicalSite::getLength(const Dimension dim) const {
	return data->clsSize[dim];
} // end method 

// -----------------------------------------------------------------------------

inline Rsyn::PhysicalSymmetry PhysicalSite::getSymmetry() const {
	return data->clsSymmetry;
} // end method 

// -----------------------------------------------------------------------------

} // end namespace
