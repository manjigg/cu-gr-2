// 文件功能：定义 Reader 输入加载基类，将格式读取与 PopulateRsyn 数据库填充衔接。
// 【零基础阅读】Reader 是读取器公共协议，各格式读取器提供自己的 load 方法。
// 参数用 JSON 传文件名/选项，解析后借 PopulateRsyn 把描述装进共享设计。
// 继承基类共用填充能力，不等于创建 Reader 基类就自动读取所有格式。
/* Copyright 2014-2018 Rsyn
 * params 为 JSON 输入设置，design 为待填逻辑数据库；模板/描述字段不是最终路由树。
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
 * File:   Reader.h
 * Author: jucemar
 *
 * Created on 18 de Fevereiro de 2017, 08:20
 */

#ifndef RSYN_READER_H
#define RSYN_READER_H

#include "rsyn/util/Json.h"
#include "rsyn/io/reader/PopulateRsyn.h"

namespace Rsyn {

class Session;


class Reader : public PopulateRsyn {
public:
	virtual bool load(const Rsyn::Json &params) = 0;
}; // end class

} // end namespace


#endif /* RSYN_READER_H */
