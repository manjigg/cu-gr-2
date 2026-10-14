// 文件功能：注册内置物理设计和布线 guide 服务，供 Session 按服务名启动。
// 【零基础阅读】登记物理设计/guide 等服务工厂，让 Session 可按名称创建服务。
// 服务类说明柜台做什么，注册说明怎样找到它，两层职责不同。
// startService 才会启动相应对象；头文件 include 不是服务启动操作。
/* Copyright 2014-2018 Rsyn
 * rsyn.physical/rsyn.routingGuide 是共享服务名，注册函数创建相应派生服务，不立即读取全部设计。
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
 
// HOW-TO
// To register a step, just include its .h below and add a call to
// "registerService<T>(name)" where T is the service class name and name how
// the service will be referred to.

#include <Rsyn/Session>

// Services
#include "rsyn/phy/PhysicalService.h"
#include "rsyn/ispd18/RoutingGuide.h"

// Registration
namespace Rsyn {
void Session::registerServices() {
	registerService<Rsyn::PhysicalService>("rsyn.physical");
	registerService<Rsyn::RoutingGuide>("rsyn.routingGuide");
} // end method
} // end namespace
