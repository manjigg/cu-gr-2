// 文件功能：读取并解析布尔/整数/浮点/字符串环境变量，缺失或解析失败时返回默认值。
// 【零基础阅读】环境变量是终端给程序的名字/文字配置，getenv 取原文字。
// 输入流将文字解析成整数/小数/布尔等类型，缺失或失败返回调用者给的默认值。
// 例如 "8" 解析后才是数字 8，这与读取 LEF/DEF 设计文本不是同一个流程。
/* Copyright 2014-2018 Rsyn
 * name 环境变量名，defaultValue 缺失/解析失败时返回值，value 为 getenv 取出的文字地址。
 * str 待解析文字，iss 字符串输入流，data 转换后变量；解析成功才用 data，否则用默认值。
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
 
/* 
 * File:   Environment.h
 * Author: gaflach
 *
 * Created on February 25, 2016, 6:56 PM
 */

#ifndef UTIL_ENVIRONMENT_H
#define UTIL_ENVIRONMENT_H

#include <sstream>
#include <cstdlib>

class Environment {
private:
	
	template<typename T>
	static bool parse(const std::string &str, T &data) {
		std::istringstream iss( str );
		iss >> data;
		return !iss.fail();
	} // end method		
	
	template<typename T>
	static T get(const std::string &name, const T &defaultValue) {
		T data;
		const char *value = std::getenv(name.c_str());
		return value && parse(value, data)? data : defaultValue;
	} // end method
		
public:

	static int getBoolean(const std::string &name, const bool defaultValue) { return get(name, defaultValue); }
	static int getInteger(const std::string &name, const int defaultValue) { return get(name, defaultValue); }
	static float getFloat(const std::string &name, const float defaultValue) { return get(name, defaultValue); }
	static double getDouble(const std::string &name, const double defaultValue) { return get(name, defaultValue); }
	static std::string getString(const std::string &name, const std::string &defaultValue) { return get(name, defaultValue); }
	
}; // end class


#endif
