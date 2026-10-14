// 文件功能：实现工艺层索引、名称、类型、宽度与 LEF 层读取。
// 【零基础阅读】从工艺层记录读取名字、类型、索引和底层 LEF 规则对象。
// 相对索引服务特定层类别，不能只凭总体序号猜数组下标。
// CUGR MetalLayer 查询后复制所需规则，此处只是现有资料的访问桥梁。
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
// getIndex/RelativeIndex 读取不同编号范围；clsLower/Upper 是地址，clsWidth 是 DBU，clsType 是层用途。

inline lefiLayer* PhysicalLayer::getLayer() const {
	return data->clsLayer;
} // end method 

// -----------------------------------------------------------------------------

inline std::string PhysicalLayer::getName() const {
	return data->clsName;
} // end method 

// -----------------------------------------------------------------------------

inline Rsyn::PhysicalLayerType PhysicalLayer::getType() const {
	return data->clsType;
} // end method 

// -----------------------------------------------------------------------------

inline DBU PhysicalLayer::getWidth() const {
	return data->clsWidth;
} // end method 

// -----------------------------------------------------------------------------

inline int PhysicalLayer::getIndex() const {
	return data->id;
} // end method 

// -----------------------------------------------------------------------------

inline int PhysicalLayer::getRelativeIndex() const {
	return data->clsRelativeIndex;
} // end method 

// -----------------------------------------------------------------------------

inline Rsyn::PhysicalLayer PhysicalLayer::getPhysicalLayerLower() const {
	return Rsyn::PhysicalLayer(data->clsLower);
} // end method 

// -----------------------------------------------------------------------------

inline Rsyn::PhysicalLayer PhysicalLayer::getPhysicalLayerUpper() const {
	return Rsyn::PhysicalLayer(data->clsUpper);
} // end method 

// -----------------------------------------------------------------------------

} // end namespace
