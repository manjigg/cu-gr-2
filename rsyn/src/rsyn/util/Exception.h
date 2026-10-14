// 文件功能：定义 Rsyn 通用异常及错误信息封装。
// 【零基础阅读】异常类型包装错误信息，方法 throw 后由外层 catch 决定怎样处理。
// 类型可区分错误类别，消息提供文字说明，不是普通日志输出本身。
// 这里定义通用封装，具体错误发生位置在调用这些类型的代码里。
/* Copyright 2014-2018 Rsyn
 * 异常对象保存错误说明，what 一类查询给文字；错误类型区分失败类别，不是算法返回评分。
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
 
#ifndef EXCEPTION_H
#define	EXCEPTION_H

#include <exception>
#include <string>
#include <ostream>

class Exception: public std::exception {
	friend std::ostream &operator<<(std::ostream &out, const Exception &e) { 
		return out << e.what();
	} // end method	
	
public:

	explicit Exception(const std::string& message) :  clsMsg(message) {}
	virtual ~Exception() throw (){}

	virtual const char* what() const throw (){
       return clsMsg.c_str();
    } // end method
	
protected:
    std::string clsMsg;
}; // end class

#endif
