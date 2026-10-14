// 文件功能：声明路由 guide 服务，提供 guide 导入及按逻辑线网查询矩形集合的接口。
// 【零基础阅读】guide 服务像按线网整理的走廊档案柜，负责加载和查询区域。
// 描述中的名字需查设计里的 Net/Layer，之后才建立可靠对象关联。
// 这是已有 guide 的管理接口，新的 CUGR 输出由 GlobalRouter 生成。
/* Copyright 2014-2018 Rsyn
 * 描述网名/层名先查现有 Net/Layer，再把矩形存为该网属性；输入为空不自动求新路线。
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
 * File:   RoutingGuide.h
 * Author: jucemar
 *
 * Created on 21 de Dezembro de 2016, 17:47
 */

#ifndef RSYN_ISPD18_ROUTING_GUIDE_H
#define RSYN_ISPD18_ROUTING_GUIDE_H

#include "rsyn/session/Service.h"
#include <Rsyn/Session>
#include "rsyn/ispd18/Guide.h"
#include "rsyn/io/parser/guide-ispd18/GuideDescriptor.h"

namespace Rsyn {

class RoutingGuide : public Rsyn::Service {
protected:
	Rsyn::Session clsSession;
	Rsyn::Design clsDesign;
	Rsyn::Module clsModule;
	Rsyn::PhysicalDesign clsPhDesign;
	Rsyn::Attribute<Rsyn::Net, Rsyn::NetGuide> clsGuides;
	bool clsInitialized  = false;
public:
	RoutingGuide() = default;
	void start(const Rsyn::Json &params);
	void stop();
	
	void loadGuides(const GuideDscp & dscp);
	
	const NetGuide & getGuide(Rsyn::Net net) const {
		return clsGuides[net];
	}
        
        void updateGuide(Rsyn::Net net, Rsyn::NetGuide guide) {
            clsGuides[net] = guide;
        }
}; // end class 

} // end namespace 


#endif /* ISPD18_ROUTINGGUIDE */
