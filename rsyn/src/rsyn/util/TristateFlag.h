// 文件功能：实现未指定/真/假三态标志，用 specified 区分默认值与显式设置。
// 【零基础阅读】普通 bool 只有真假，配置还常需知道“用户是否明确设置过”。
// specified 记是否给值，flag 记真假，组合得到未指定/真/假三态。
// clear 恢复未指定，与 set(false) 明确设置假不同，这是两个状态位存在的理由。
/* Copyright 2014-2018 Rsyn
 * specified 是否显式指定，flag 实际真假；set(false) 为指定假，clear 为未指定。
 * :1 是位域一位，不是初始化为 1；构造函数才决定初始 false/false。
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
 
#ifndef RSYN_TRISTATE_FLAG_H
#define RSYN_TRISTATE_FLAG_H

namespace Rsyn {

class TristateFlag {
private:
	bool specified : 1;
	bool flag : 1;

public:

	TristateFlag() : specified(false), flag(false) {}
	TristateFlag(const bool value) : specified(true), flag(value) {}

	bool get() const {
		return flag;
	} // end method

	void set(const bool value) {
		specified = true;
		flag = value;
	} // end method

	void clear() {
		specified = false;
		flag = false;
	} // end method

	bool isSpecified() const {
		return specified;
	} // end method

	bool isNotSpecified() const {
		return !specified;
	} // end method

	operator bool() const { 
		return get();
	} // end method
}; // end class

} // end namespace

#endif 
