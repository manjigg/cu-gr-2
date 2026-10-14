// 文件功能：定义轻量数据指针代理，提供空值判断、对象身份比较及基于 id 的排序/哈希。
// 代理复制共享底层数据，实际对象生命周期由数据库管理。
// 【零基础阅读】代理是薄薄一层访问钥匙，data 指向真正资料；多把钥匙可指同一资料。
// -> 访问底层字段，bool 判断空值，id 用于稳定身份比较，避免按地址造成顺序变化。
// 代理不负责让底层数据永远存在，设计管理者的生命周期必须覆盖使用过程。
/* Copyright 2014-2018 Rsyn
 * data 是底层资料地址，T 模板指定资料类型，other/left/right 为比较的代理，id 稳定身份。
 * !/bool 判断地址是否为空，== 判断是否同一资料，< 按 id 排序，std::hash 也按 id 生成键。
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
 
#ifndef RSYN_PROXY_H
#define RSYN_PROXY_H

namespace Rsyn {

template<class T>
class Proxy {
friend struct std::hash<Rsyn::Proxy<T>>;
protected:
	T * data;

	T *operator->() { return data; }
	const T *operator->() const { return data; }

	// For some reason the compiler allows ElementPointer to be silently casted
	// to int, which led to hard to find bugs. Now we explicitly tell the
	// compiler to not allow such conversion by making the cast operator
	// private.
	operator int() const; // not implemented

	// Returns the pointer to the data stored by this proxy.
	T *getData() { return data; }
	const T *getData() const { return data; }

public:

	Proxy() : data(nullptr) {}
	Proxy(std::nullptr_t) : data(nullptr) {}
	Proxy(T * data) : data(data) {}

	bool operator!() const { return data == nullptr; };
	operator bool() const { return data; }

	bool operator==(const Proxy<T> &other) const { return data == other.data; }
	bool operator!=(const Proxy<T> &other) const { return data != other.data; }

	// Used in map-like data structures.
	friend bool operator<(const Proxy<T> &left, const Proxy<T> &right) {
		// [IMPORTANT] We don't use the pointer (e) directly to avoid
		// non-determinism behavior. Note that the pointer address may change
		// from execution to execution and hence the mapping function may return
		// elements in different order leading to different results.
		return left.data->id < right.data->id;
	} // end method
}; // end class

} // end namespace

namespace std {
//! @brief Specialization of hash functor to allow Rsyn objects to be used
//!        in unordered collections.
template <typename T>
struct hash<Rsyn::Proxy<T>> {
	size_t operator()(const Rsyn::Proxy<T> &proxy) const {
		return hash<int>()(proxy.data->id);
	} // end operator
}; // end struct
} // end namespace

#endif
