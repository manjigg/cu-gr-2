// 文件功能：声明 Rsyn 数据库持久化的保存/加载入口。
// 【零基础阅读】声明按 key 读写可序列化对象的接口，像按档案名保存/取回资料。
// Serializable 约定对象怎样与流交换字段，数据库负责按名字组织存取。
// 当前 Database.cpp 仅占位，不能从声明存在就断言完整持久化已实现。
/* Copyright 2014-2018 Rsyn
 * key 记录名字，data 要读/写的 Serializable 对象；同名 data 在其他模块可能是指针，须看类型。
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

#ifndef RSYN_DATABASE_H
#define RSYN_DATABASE_H

#include <string>

#include <Serializable.h>
#include <SerializationStream.h>

namespace Rsyn {
class Database {
public:

	void read(const std::string &key, Serializable &data);
	void write(const std::string &key, const Serializable &data);

};
} // end namespace

#endif 
