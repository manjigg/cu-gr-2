// 文件功能：声明过孔的层、几何集合和规则生成参数接口。
// 【零基础阅读】过孔接口分别提供底层、切孔层、顶层及各层形状。
// 生成式过孔还需看 has* 标志和尺寸/间距/包围参数，不能只遍历固定矩形。
// 本代理描述一类过孔几何，实际放置位置来自路由点/PhysicalRoutingVia。
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
 * File:   PhysicalVia.h
 * Author: jucemar
 *
 * Created on 14 de Maio de 2017, 15:03
 */

#ifndef PHYSICALDESIGN_PHYSICALVIA_H
#define PHYSICALDESIGN_PHYSICALVIA_H

namespace Rsyn {
// clsLayers/clsViaGeometries 第一索引为底/切/顶；clsEnclosure/clsOffset 第一索引仅为底/顶金属。
// clsCutSize/Spacing/Origin 第二轴 0=x/1=y，单位 DBU；clsNumRows/Cols 为阵列数量，has* 表示可选项。

class PhysicalVia : public Proxy<PhysicalViaData> {
	friend class PhysicalDesign;
	friend class PhysicalDesignData;
	RSYN_FRIEND_OF_GENERIC_LIST_COLLECTION;
protected:
	//! @brief Constructs a Rsyn::PhysicalVia object with a pointer to Rsyn::PhysicalViaData.

	PhysicalVia(PhysicalViaData * data) : Proxy(data) {
	}
public:
	//! @brief Constructs a Rsyn::PhysicalVia object with a null pointer to Rsyn::PhysicalViaData.

	PhysicalVia() : Proxy(nullptr) {
	}
	//! @brief Constructs a Rsyn::PhysicalVia object with a null pointer to Rsyn::PhysicalViaData.

	PhysicalVia(std::nullptr_t) : Proxy(nullptr) {
	}

	//! @brief Returns via name
	const std::string & getName() const;

	//! @brief returns the top layer of the via
	Rsyn::PhysicalLayer getTopLayer() const;
	//! @brief returns the cut layer of the via
	Rsyn::PhysicalLayer getCutLayer() const;
	//! @brief returns the bottom layer of the via
	Rsyn::PhysicalLayer getBottomLayer() const;
	//! @brief returns the respective layer
	Rsyn::PhysicalLayer getLayer(const PhysicalViaLayerType type) const;
	
	bool isViaDesign() const;
	bool isViaRule() const;
	bool isViaGeometry() const;
	bool hasViaRule() const;
	bool hasRowCol() const;
	bool hasOrigin() const;
	bool hasOffset() const;
	bool hasPattern() const;
	ViaType getViaType() const;
	Rsyn::PhysicalViaRuleBase getViaRule() const;
	DBU getCutSize(const Dimension dim) const;
	DBU getSpacing(const Dimension dim) const;
	DBU getEnclosure(const ViaLevel level, const Dimension dim) const;
	DBU getOrigin(const Dimension dim) const;
	DBU getOffset(const ViaLevel level, const Dimension dim) const;
	int getNumRows() const;
	int getNumCols() const;
	const std::string & getPattern() const;
	const std::vector<Rsyn::PhysicalViaGeometry>  & allBottomGeometries() const;
	const std::vector<Rsyn::PhysicalViaGeometry>  & allCutGeometries() const;
	const std::vector<Rsyn::PhysicalViaGeometry>  & allTopGeometries() const;
	const std::vector<Rsyn::PhysicalViaGeometry>  & allGeometries(const PhysicalViaLayerType layer) const;
}; // end class 

} // end namespace 

#endif /* PHYSICALDESIGN_PHYSICALVIA_H */
