// 文件功能：导入第三方 JSON 类型并提供 Rsyn::Json 别名，传递 Reader/Service 参数。
// 【零基础阅读】JSON 像按字段名填写的表单，可以装文字、数字、列表或嵌套对象。
// Rsyn::Json 是第三方类型的项目别名，Reader/Service 用它传参数。
// 这里引入并命名类型，不会读取设计；解析/序列化实现见 3rdparty/json/json.hpp。
/* Copyright 2014-2018 Rsyn
 * 本文件为类型别名入口，没有自己的 m_type/m_value；真实 JSON 字段在第三方 basic_json 中。
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
 
#ifndef RSYN_JSON_H
#define RSYN_JSON_H

#include "rsyn/3rdparty/json/json.hpp"

namespace Rsyn {
typedef nlohmann::json Json;
} // end namespace

#endif /* JSON_H */
