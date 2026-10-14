// 文件功能：实现障碍工艺层和矩形集合读取。
// 【零基础阅读】从障碍记录返回所属层和矩形集合，供容量估计读取。
// 这里得到原始边界；加间距、折算占用轨道是 GridGraph 中的后续步骤。
// allBounds 的多个矩形需分别处理，不能把集合名字当成单个 Bounds。
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
// clsLayer 层代理，clsBounds 多块 DBU 矩形；返回范围后由调用者 for 逐形状扣减估计容量。

inline Rsyn::PhysicalLayer PhysicalObstacle::getLayer() const {
	return data->clsLayer;
} // end method 

// -----------------------------------------------------------------------------

inline const std::vector<Bounds> & PhysicalObstacle::allBounds() const {
	return data->clsBounds;
} // end method 

// -----------------------------------------------------------------------------

inline std::size_t PhysicalObstacle::getNumObs() const {
	return data->clsBounds.size();
} // end method 

// -----------------------------------------------------------------------------

inline bool PhysicalObstacle::hasLayer() const {
	return data->clsLayer != nullptr;
} // end method 

// -----------------------------------------------------------------------------

} // end namespace
