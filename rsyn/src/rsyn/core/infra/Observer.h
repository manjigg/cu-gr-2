// 文件功能：定义设计观察者回调接口，在实例/线网/引脚发生变更时通知依赖服务。
// 【零基础阅读】观察者是“发生变更后通知我”的接口，服务可实现相应回调。
// 比如实例变更后物理资料需要更新，通知让依赖模块知道何时刷新。
// virtual 表示允许派生观察者提供自己的响应；接口不固定一种算法行为。
/* Copyright 2014-2018 Rsyn
 * 回调的实例/网/引脚参数指出发生事件的对象，事件类型选择响应，参数不是新建对象指令。
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
 
#ifndef RSYN_DESIGN_OBSERVER_H
#define RSYN_DESIGN_OBSERVER_H

namespace Rsyn {

class DesignObserver {
friend class Rsyn::Design;
private:

	Design observedDesign;

public:
	
	// Note: The observer will not be registered to receive notifications for
	// methods that it does not overwrite. Therefore, no runtime overhead for
	// handling undesired notifications.

	virtual void
	onDesignDestruction() {}

	virtual void
	onPostInstanceCreate(Rsyn::Instance instance) {}

	virtual void
	onPreInstanceRemove(Rsyn::Instance instance) {}

	virtual void
	onPostNetCreate(Rsyn::Net net) {}

	virtual void
	onPreNetRemove(Rsyn::Net net) {}

	virtual void
	onPostCellRemap(Rsyn::Cell cell, Rsyn::LibraryCell oldLibraryCell) {}

	virtual void
	onPostPinConnect(Rsyn::Pin pin) {}

	virtual void
	onPrePinDisconnect(Rsyn::Pin pin) {}

	virtual void
	onPreInstanceMove(Rsyn::Instance) {}

	virtual void
	onPostInstanceMove(Rsyn::Instance) {}

	virtual
	~DesignObserver() {
		if (observedDesign)
			observedDesign.unregisterObserver(this);
	} // end destructor

}; // end class

} // end namespace

#endif
