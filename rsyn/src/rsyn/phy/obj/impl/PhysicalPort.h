// 文件功能：实现端口的位置、朝向、边界和工艺层读取。
// 【零基础阅读】从端口记录读位置、朝向、层与边界，供外部接线口几何计算。
// 端口几何与普通单元模板流程不同，CUGR Instance.cpp 专门处理它。
// get* 方法不移动端口，只把导入阶段保存的资料取出。
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
 * File:   PhysicalPort.h
 * Author: jucemar
 *
 * Created on 4 de Outubro de 2016, 19:34
 */

namespace Rsyn {
// position/bounds DBU 位置/边界，orientation 姿态，clsPortLayer 层；端口不走普通库引脚下标链。
// -----------------------------------------------------------------------------

inline PhysicalLayer PhysicalPort::getLayer() const {
	return data->clsPortLayer;
} // end method 

// -----------------------------------------------------------------------------

inline bool PhysicalPort::hasLayer() const {
	return data->clsPortLayer != nullptr;
} // end method 

// -----------------------------------------------------------------------------

inline PhysicalOrientation PhysicalPort::getOrientation() const {
	return getInstance().getOrientation();
} // end method 

// -----------------------------------------------------------------------------

} // end namespace 
