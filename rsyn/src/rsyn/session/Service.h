// 文件功能：定义服务的启动/停止生命周期和请求类型，供 Session 管理物理/guide 服务。
// 【零基础阅读】服务协议规定如何启动/停止；Session 按名称管理各种服务。
// virtual 方法允许物理/guide 等不同服务各自实现初始化与清理。
// 公共协议不固定服务内容，实际数据与功能请进入相应派生服务。
/* Copyright 2014-2018 Rsyn
 * params 服务启动设置，requestType 请求类别，Session 管生命周期；配置不是全部物理资料。
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
 
#ifndef RSYN_SERVICE_H
#define RSYN_SERVICE_H

#include "rsyn/util/Json.h"

namespace Rsyn {

class Session;


enum ServiceRequestType {
	SERVICE_OPTIONAL,
	SERVICE_MANDATORY
}; // end enum

class Service {
public:
	virtual void start(const Rsyn::Json &params) = 0;
	virtual void stop() = 0;
}; // end class

} // end namespace


#endif
