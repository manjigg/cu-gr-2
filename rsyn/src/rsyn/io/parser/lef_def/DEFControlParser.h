// 文件功能：声明 DEF 回调适配器，将底层解析事件整理成 DefDscp 中间描述。
// 【零基础阅读】控制解析器负责注册回调并定义怎样把 DEF 库对象转换成描述字段。
// 回调是底层解析器在读到一项时调用的用户函数，不是源文件按顺序自动执行。
// 接口在此，具体转换在 .cpp，语法识别实现来自 libdef.a。
/* Copyright 2014-2018 Rsyn
 * 回调输入 defi* 当前记录、类型标志与用户上下文，输出 DefDscp；回调编号不是芯片层号。
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
 
#ifndef DEFCONTROLPARSER_H
#define	DEFCONTROLPARSER_H

#include <string>
	using std::string;
#include <iostream>
	using std::cout;
#include <vector>
	using std::vector;

#include "rsyn/io/legacy/PlacerInternals.h"

//! DEF files must be parsed after LEF files
	
#include "rsyn/phy/util/DefDescriptors.h"

class DEFControlParser {
public:
	DEFControlParser();
	void parseDEF(const std::string &filename, DefDscp &defDscp) ;
	void writeDEF(const std::string &filename, const std::string designName, const std::vector<DefComponentDscp> &components);
	void writeFullDEF(const std::string &filename, const DefDscp & defDscp);
	virtual ~DEFControlParser();
	
	static std::string unescape(const std::string &str);
	
}; // end class

#endif	/* DEFCONTROLPARSER_H */
