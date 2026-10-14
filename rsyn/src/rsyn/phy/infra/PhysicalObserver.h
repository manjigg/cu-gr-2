// 文件功能：定义物理设计变更观察者的回调接口。
// 【零基础阅读】观察者约定物理对象变化时可通知哪些回调，例如位置更新。
// 注册服务接到通知后自行刷新资料，这就是“事件驱动”，不是不断轮询每个单元。
// virtual 方法允许不同观察者实现不同响应，接口本身不规定统一路由算法。
/* Copyright 2014-2018 Rsyn
 * 事件对象参数标明哪个实例/物理对象变化，位置是 DBU 向量，事件类别决定回调而非金属层。
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
 
#ifndef RSYN_PHYSICAL_OBSERVER_H
#define RSYN_PHYSICAL_OBSERVER_H

namespace Rsyn {

class PhysicalDesignObserver {
friend class Rsyn::PhysicalDesign;
private:
	PhysicalDesign clsPhDesign;

public:
	
	// Note: The observer will not be registered to receive notifications for
	// methods that it does not overwrite. Therefore, no runtime overhead for
	// handling undesired notifications.

	virtual void
	onPhysicalDesignDestruction() {}

	virtual void
	onPostNetRoutingChange(Rsyn::PhysicalNet physicalNet) {}
	
	virtual
	~PhysicalDesignObserver() {
		if (clsPhDesign)
			clsPhDesign.unregisterObserver(this);
	} // end destructor

}; // end class

} // end namespace


#endif /* RSYN_PHYSICALOBSERVER_H */
