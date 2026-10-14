// 文件功能：声明特殊线网 DEF 描述和特殊导线集合接口。
// 【零基础阅读】特殊线网接口提供 DEF 原描述和已有特殊导线集合。
// CUGR 按其路径点/过孔提取固定金属障碍，不把它当普通信号网重布。
// getNet 此处的返回含义应看声明，不能仅按名字猜成逻辑 Net 代理。
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
 * File:   PhysicalSpecialNet.h
 * Author: jucemar
 *
 * Created on 23 de Maio de 2017, 20:58
 */

#ifndef PHYSICALDESIGN_PHYSICALSPECIALNET_H
#define PHYSICALDESIGN_PHYSICALSPECIALNET_H

namespace Rsyn {
// net 描述保留原 DEF 名称/导线资料；wire/segment/point 遍历路径层级，不是普通信号接入点表。

class PhysicalSpecialNet : public Proxy<PhysicalSpecialNetData> {
	friend class PhysicalDesign;
	friend class PhysicalDesignData;
protected:
	//! @brief Constructs a Rsyn::PhysicalSpecialNet object with a pointer to Rsyn::PhysicalSpecialNetData.

	PhysicalSpecialNet(PhysicalSpecialNetData * data) : Proxy(data) {
	}
public:
	//! @brief Constructs a Rsyn::PhysicalSpecialNet object with a null pointer to Rsyn::PhysicalSpecialNetData.

	PhysicalSpecialNet() : Proxy(nullptr) {
	}
	//! @brief Constructs a Rsyn::PhysicalSpecialNet object with a null pointer to Rsyn::PhysicalSpecialNetData.

	PhysicalSpecialNet(std::nullptr_t) : Proxy(nullptr) {
	}
	
	const DefNetDscp & getNet() const;
}; // end class 

} // end namespace 

#endif /* PHYSICALDESIGN_PHYSICALSPECIALNET_H */
