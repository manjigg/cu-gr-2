// 文件功能：声明物理设计服务，向 Session 暴露 PhysicalDesign。
// 【零基础阅读】服务像共享柜台，Session 通过名字找到它，取得其 PhysicalDesign。
// start/stop 约定生命周期，观察者接口让逻辑变更能同步到物理资料。
// 这是服务接口，不能因为创建一个代理就认为 LEF/DEF 已经加载完。
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
 
#ifndef RSYN_PHYSICAL_SERVICE_H
#define RSYN_PHYSICAL_SERVICE_H

#include "rsyn/session/Service.h"
#include <Rsyn/PhysicalDesign>

namespace Rsyn {
// session 提供共享逻辑设计，physicalDesign 关联物理资料，params 为功能开关 JSON，不是 LEF 正文。

class PhysicalService : public Rsyn::Service, public DesignObserver {
private:
	
	Rsyn::PhysicalDesign clsPhysicalDesign;
	Rsyn::Design clsDesign;
	
public:
	
	//! @brief Start method to the Rsyn::PhysicalDesign service
	virtual void start(const Rsyn::Json &params);
	
	//! @brief Stop method to the Rsyn::PhysicalDesign service
	virtual void stop();
	
	//! @brief Getting the Rsyn::PhysicalDesign object.
	Rsyn::PhysicalDesign getPhysicalDesign() { return clsPhysicalDesign; }
	
	// Events
	virtual void
	onPostCellRemap(Rsyn::Cell cell, Rsyn::LibraryCell oldLibraryCell) override;
	
	virtual void 
	onPostInstanceCreate(Rsyn::Instance instance) override;
}; // end class

} // end namespace

#endif
