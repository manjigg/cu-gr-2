// 文件功能：转换旧格式字符串中的引脚方向/用途与 Rsyn 枚举，兼容 LEF 和 Bookshelf 导入。
// 【零基础阅读】把 "INPUT"/"POWER" 等外部文字转换为 Rsyn 的方向/用途枚举。
// if 比较字符串选择类别，未知文字返回未知值；不要把它强行视为默认合法类别。
// Bookshelf 是另一种输入约定，转换规则不一定与 LEF 完全相同。
/* Copyright 2014-2018 Rsyn
 * direction 在这些转换函数为输入文字，返回 Direction/Use 为枚举；未知文字返回未知类别。
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
 
#ifndef RSYN_LEGACY_H
#define RSYN_LEGACY_H

#include <string>

#include "rsyn/core/Rsyn.h"

namespace Legacy {

inline Rsyn::Direction lefPinDirectionFromString(const std::string &direction) {
	if (direction == "INPUT"   ) return Rsyn::IN;
	if (direction.substr(0, 6) == "OUTPUT"  ) return Rsyn::OUT;
	if (direction == "INOUT"   ) return Rsyn::BIDIRECTIONAL;
	if (direction == "FEEDTHRU") return Rsyn::UNKNOWN_DIRECTION;
	return Rsyn::UNKNOWN_DIRECTION;
} // end function

inline Rsyn::Use lefPinUseFromString(const std::string &direction) {
	if (direction == "SIGNAL"   ) return Rsyn::SIGNAL;
	if (direction == "POWER"    ) return Rsyn::POWER;
	if (direction == "GROUND"  ) return Rsyn::GROUND;
	if (direction == "CLOCK") return Rsyn::CLOCK;
	if (direction == "TIEOFF") return Rsyn::TIEOFF;
	if (direction == "ANALOG") return Rsyn::ANALOG;
	if (direction == "SCAN") return Rsyn::SCAN;
	if (direction == "RESET") return Rsyn::RESET;
	return Rsyn::UNKNOWN_USE;
} // end function


inline Rsyn::Direction bookshelfPinDirectionFromString(const std::string &direction) {
	if (direction == "I") return Rsyn::IN;
	if (direction == "O") return Rsyn::OUT;
	if (direction == "B") return Rsyn::OUT; // Due to Rsyn restrictions, I am considering the Bidirectional is only output.
	return Rsyn::UNKNOWN_DIRECTION;
} // end function

inline std::string bookshelfPinDirectionToString(const Rsyn::Direction direction) {
	if (direction == Rsyn::IN) return "I";
	if (direction == Rsyn::OUT) return "O";
	if (direction == Rsyn::BIDIRECTIONAL) return "B";
	return "<DIRECTION_UNKNOWN>";
} // end function

} // end namespace

#endif
