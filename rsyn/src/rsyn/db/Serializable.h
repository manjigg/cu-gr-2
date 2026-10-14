// 文件功能：声明可序列化对象的公共接口，约定对象与序列化流之间的数据读写。
// 【零基础阅读】纯虚函数 =0 约定派生对象必须实现怎样从流读/往流写自己的字段。
// 序列化是把内存资料变成可存储格式，反序列化是重新装回对象。
// 本文件只定义协议，不提供每类设计对象的完整读写实现。
/* Copyright 2014-2018 Rsyn
 * stream 输入/输出序列化介质；read/write 为纯虚协议，具体字段顺序由派生实现规定。
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

#ifndef RSYN_SERIALIZABLE_H
#define RSYN_SERIALIZABLE_H

#include "SerializationStream.h"

namespace Rsyn {
class Serializable {
public:
	virtual void read(Rsyn::SerializationStream &stream) = 0;
	virtual void write(Rsyn::SerializationStream &stream) = 0;
}; // end class
}

#endif /* SERIALIZABLE_H */
