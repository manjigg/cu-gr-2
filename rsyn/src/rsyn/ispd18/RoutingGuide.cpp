// 文件功能：实现 guide 服务生命周期，将解析矩形按线网/工艺层映射到设计对象。
// 【零基础阅读】启动服务准备属性存储，加载时把每网矩形放到对应逻辑线网档案。
// 层名也要查工艺层对象，文字名字与层索引不是天然相同的值。
// 本文件不优化这些区域的路线，只按描述组织已有 guide。
/* Copyright 2014-2018 Rsyn
 * session 取共享设计，网代理作属性键，guide 描述提供区域；循环追加已有区域而不搜索路径。
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
 * File:   RoutingGuide.cpp
 * Author: jucemar
 *
 * Created on 21 de Dezembro de 2016, 17:47
 */

#include "RoutingGuide.h"
#include <Rsyn/PhysicalDesign>

namespace Rsyn {

void RoutingGuide::start(const Rsyn::Json &params) {
	if(clsInitialized)
		return;
	clsDesign = clsSession.getDesign();
	clsModule = clsSession.getTopModule();
	clsPhDesign = clsSession.getPhysicalDesign();

	clsGuides = clsDesign.createAttribute();

	clsInitialized = true;
} // end method

// -----------------------------------------------------------------------------

void RoutingGuide::stop() {
} // end method

// -----------------------------------------------------------------------------

void RoutingGuide::loadGuides(const GuideDscp & dscp) {
	for (const GuideNetDscp & netDscp : dscp.clsNetGuides) {
		Rsyn::Net net = clsDesign.findNetByName(netDscp.clsNetName);
		if (net) {
                        int id = 0;
			NetGuide & netGuide = clsGuides[net];
			std::vector<LayerGuide> & layerGuides= netGuide.clsLayerGuides;
			layerGuides.reserve(netDscp.clsLayerDscps.size());
			for (const GuideLayerDscp & layerDscp : netDscp.clsLayerDscps) {
				layerGuides.push_back(LayerGuide());
				LayerGuide & layerGuide = layerGuides.back();
                                layerGuide.clsId = id++;
				layerGuide.clsBounds = layerDscp.clsLayerGuide;
				layerGuide.clsPhLayer = clsPhDesign.getPhysicalLayerByName(layerDscp.clsLayer);
			} // end for
		} else {
			std::cout << "WARNING: Net '" << netDscp.clsNetName << "' does not exist and the routing guide is being ignored.\n";
		} // end else
	} // end for 
} // end method

// -----------------------------------------------------------------------------

} // end namespace
