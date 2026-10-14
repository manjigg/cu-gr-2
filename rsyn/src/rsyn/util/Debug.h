// 文件功能：定义 Rsyn 调试断言和错误定位辅助设施。
// 【零基础阅读】断言检查程序内部约定，如“下标应在范围内”，便于开发时定位错误。
// 宏可在编译设置下启用/禁用，不能把断言当成完整用户输入校验。
// 本文件提供调试助手，调用处的条件才说明它检查哪个具体预期。
/* Copyright 2014-2018 Rsyn
 * 条件参数为真假表达式，消息/文件/行号供诊断；行号是源码位置，不是网格 y 坐标。
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

#ifndef RSYN_ASSERT_H
#define RSYN_ASSERT_H

#include <iostream>

#define rsynAbort(msg) \
	std::cout << "\n"; \
	std::cout << "ASSERTION: " << msg << "\n"; \
	std::cout << __FILE__  << ":" << __LINE__ << "\n"; \
	std::cout << std::flush; \
	std::exit(1);

#define rsynAssert(condition, msg) \
	if (!(condition)) { \
		std::cout << "\n"; \
		std::cout << "ASSERTION: " << #condition << "\n"; \
		std::cout << __FILE__  << ":" << __LINE__ << "\n"; \
		std::cout << msg << "\n"; \
		std::cout << std::flush; \
		std::exit(1); \
	} // end if

#endif
