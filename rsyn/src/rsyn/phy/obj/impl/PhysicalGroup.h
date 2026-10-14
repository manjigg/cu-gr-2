// 文件功能：实现物理分组的成员和约束关联读取。
// 【零基础阅读】按代理读取分组成员和约束关联，成员集合不是单个空间矩形。
// 返回区域代理后可继续查询空间边界，再由调用方应用约束。
// 内联查询是代码复用方式，不会在 include 时自动改变布局。
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
 * File:   PhysicalGroup.h
 * Author: jucemar
 *
 * Created on 09 de Abril de 2017, 16:45
 */
namespace Rsyn {
// data->clsPatterns 返回名字模式表，clsRegion 返回区域代理；并不在 get 时重排/移动组成员。

// -----------------------------------------------------------------------------

inline const std::string & PhysicalGroup::getName() const {
	return data->clsName;
} // end method 

// -----------------------------------------------------------------------------

inline Rsyn::PhysicalRegion PhysicalGroup::getPhysicalRegion() const {
	return data->clsRegion;
} // end method 

// -----------------------------------------------------------------------------

inline const std::vector<std::string> & PhysicalGroup::allPatterns() const {
	return data->clsPatterns;
} // end method 


// -----------------------------------------------------------------------------

} // end namespace
