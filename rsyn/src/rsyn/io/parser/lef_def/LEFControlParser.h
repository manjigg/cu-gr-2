// 文件功能：声明 LEF 回调适配器，将工艺层和库单元解析结果整理成 LefDscp。
// 【零基础阅读】LEF 控制解析器把工艺层/库单元/引脚等事件转成库描述。
// 注册函数说明谁处理哪类对象，读取入口触发底层 liblef 的实际语法解析。
// 本文件先看 parseLEF 声明，再到 .cpp 追注册和各回调的字段转换。
/* Copyright 2014-2018 Rsyn
 * 输入 lefi* 为刚解析的层/模板，LefDscp 接收结果；文件名是路径而非模板名。
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
 
#ifndef LEFCONTROLPARSER_H
#define LEFCONTROLPARSER_H

#include <string>

#include "rsyn/phy/util/LefDescriptors.h"

//! LEF file must be parsed first than DEF file

class LEFControlParser {
public:

	LEFControlParser() {
	}

	virtual ~LEFControlParser() = default;
	void parseLEF(const std::string &filename, LefDscp & dscp);
}; // end class

#endif /* LEFCONTROLPARSER_H */
