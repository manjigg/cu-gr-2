// 文件功能：实现工艺间距条目的规则参数读取。
// 【零基础阅读】读取工艺间距条目的参数，以便调用者知道适用的空间要求。
// 要求值与实际测到距离不同，满足规则需由几何计算比较两者。
// 本文件提供现有记录访问，不执行最终详细布线的整套规则检查。
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

namespace Rsyn {
// clsLayer1/2 关联层，clsDistance DBU 规则要求；测当前几何是否满足要求是调用方工作。

inline PhysicalLayer PhysicalSpacing::getLayer1() const {
	return data->clsLayer1;
} // end method 

// -----------------------------------------------------------------------------

inline PhysicalLayer PhysicalSpacing::getLayer2() const {
	return data->clsLayer2;
} // end method 

// -----------------------------------------------------------------------------

inline DBU PhysicalSpacing::getDistance() const {
	return data->clsDistance;
} // end method 

// -----------------------------------------------------------------------------

} // end namespace
