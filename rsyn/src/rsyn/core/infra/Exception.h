// 文件功能：定义逻辑网表核心操作使用的异常类型与错误报告约定。
// 【零基础阅读】异常是错误发生后向调用者报告的对象，throw 发出，catch 接住。
// 类中名字/消息区分错误类别，比只返回一个真假更能说明失败原因。
// 这里定义核心错误类型，发生什么错误仍由调用这些类型的方法决定。
/* Copyright 2014-2018 Rsyn
 * 错误消息为文字，错误对象类别用于上层 catch 分支；本文件不持有 capacity/demand 等资源量。
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
 
#ifndef RSYN_EXCEPTION_H
#define RSYN_EXCEPTION_H

#include <string>
#include <exception>

namespace Rsyn {

class Exception : public std::exception {
public:
	Exception(const std::string &msg) : std::exception(),  msg(msg) {}
	virtual const char* what() const throw() { return msg.c_str(); };
private:
	std::string msg;
}; // end class

// -----------------------------------------------------------------------------

class CellAlreadyExistsException : public Exception {
public:
	CellAlreadyExistsException(const std::string &name) :
			Exception("Cell '" + name + "' already exists.") {}
}; // end class

// -----------------------------------------------------------------------------

class CellNotFoundException : public Exception {
public:
	CellNotFoundException(const std::string &name) :
			Exception("Cell '" + name + "' not found.") {}
}; // end class

// -----------------------------------------------------------------------------

class LibraryCellAlreadyExistsException : public Exception {
public:
	LibraryCellAlreadyExistsException(const std::string &name) :
			Exception("Library cell '" + name + "' already exists.") {}
}; // end class

// -----------------------------------------------------------------------------

class LibraryCellNotFoundException : public Exception {
public:
	LibraryCellNotFoundException(const std::string &libCellName) :
			Exception("Library cell '" + libCellName + "' not found.") {}
}; // end class

// -----------------------------------------------------------------------------

class LibraryPinNotFoundException : public Exception {
public:
	LibraryPinNotFoundException(const std::string &libCellName, const std::string &libPinName) :
			Exception("Library pin '" + libPinName + "' not found in library cell '" + libCellName + "'.") {}
}; // end class

// -----------------------------------------------------------------------------

class NetAlreadyExistsException : public Exception {
public:
	NetAlreadyExistsException(const std::string &name) :
			Exception("Net '" + name + "' already exists.") {}
}; // end class

// -----------------------------------------------------------------------------

class PinNotFoundException : public Exception {
public:
	PinNotFoundException(const std::string &libCellName, const std::string &cellName, const std::string &pinName) :
			Exception("Pin '" + pinName + "' not found in cell '" + cellName + "'(" + libCellName + ").") {}
}; // end class

// -----------------------------------------------------------------------------

class IncompatibleLibraryCellForRemapping : public Exception {
public:
	IncompatibleLibraryCellForRemapping(const std::string &oldLibraryCell, const std::string &newLibraryCell) :
			Exception("Library cell '" + newLibraryCell + "' is not compatible to library cell '" + oldLibraryCell + "'.") {}
}; // end class

} // end namespace

#endif
