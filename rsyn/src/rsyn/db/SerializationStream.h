// 文件功能：声明序列化流的值、集合和网表对象读写接口，约定对象引用按 id 恢复。
// 【零基础阅读】流接口约定数字、列表和对象引用怎样传输，具体格式不由这些原型说明。
// 内存引用是地址，持久存储需按对象 id 保存，加载后再把 id 对应回真实对象。
// 否则把一次运行的地址写入文件，下次运行该地址可能已不再指向同一资料。
/* Copyright 2014-2018 Rsyn
 * value 输入/输出值，T 指类型，instance/net/pin/arc 等为待序列化逻辑代理，不是坐标。
 * 对象引用按 id 暂存、全库加载后再恢复地址，文件中的旧地址不能直接用于新一次运行。
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

#ifndef RSYN_SERIALIZATION_STREAM_H
#define RSYN_SERIALIZATION_STREAM_H

#include <string>
#include <vector>

// Note: Reference types are special as they are stored as an id in the stream,
// but as a pointer in the memory (for efficiency reasons). Hence they need to
// be converted to pointers once the database is loaded. We accomplish that by
// storing the id in the reference during the database loading and registering
// it for  conversion once the database is fully read. Notice that an on-the-fly
// conversion may not be always possible as the referenced value may not be
// loaded yet (so we still don't know its position in the memory to get a
// pointer).

namespace Rsyn {
class SerializationStream {
public:

	template<class T>
	void read(T &value);

	template<class T>
	void read(std::vector<T> &value);

	template<class T>
	void read(std::vector<std::vector<T>> &value);

	void read(Rsyn::Instance &instance);
	void read(Rsyn::Net &net);
	void read(Rsyn::Pin &pin);
	void read(Rsyn::Arc &arc);
	void read(Rsyn::LibraryCell &lcell);
	void read(Rsyn::LibraryPin &lpin);
	void read(Rsyn::LibraryArc &larc);

	template<class T>
	void write(const T &value);

	template<class T>
	void write(const std::vector<T> &value);

	template<class T>
	void write(const std::vector<std::vector<T>> &value);

	void write(const Rsyn::Instance &instance);
	void write(const Rsyn::Net &net);
	void write(const Rsyn::Pin &pin);
	void write(const Rsyn::Arc &arc);
	void write(const Rsyn::LibraryCell &lcell);
	void write(const Rsyn::LibraryPin &lpin);
	void write(const Rsyn::LibraryArc &larc);

private:

};
}

#endif /* SERIALIZATIONSTREAM_H */
