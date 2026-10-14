// 文件功能：提供字符串格式化与处理助手，减少日志和输入处理的重复代码。
// 【零基础阅读】字符串助手将数字/文字排版或处理，减少导入与日志重复写格式代码。
// string 是可变长文字，stringstream 可像流一样拼接再取最终字符串。
// 转换后的文字与原数值类别不同，输出/解析方向应按具体方法区分。
/* Copyright 2014-2018 Rsyn
 * 输入文字/数字经字符串流格式化；width/precision 等若为格式参数，不是芯片线宽或 DBU 精度。
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
 
#ifndef UTIL_STRING_H
#define	UTIL_STRING_H

#include <string>
#include <iostream>
#include <iomanip>
#include <sstream>

class String {
public:
	
	template< typename T >
	static bool parse(const std::string &str, T &data) {
		std::istringstream iss( str );
		iss >> data;
		return !iss.fail();
	} // end function	

	static bool parse(const std::string &str, std::string &data) { 
		return (data=str, true); 
	} // end function
	
	static void printCentered(std::ostream &out, const std::string &text, const int width = 80) {
		// DOES NOT WORKS WHEN TEXT HAS \t
		const int start = (width - text.size()) / 2;
		if ( start > 0 )
			out << std::setw(start) << std::setfill( ' ' ) << ' ';
		out << text;
	} // end function		
	
}; // end class

#endif
