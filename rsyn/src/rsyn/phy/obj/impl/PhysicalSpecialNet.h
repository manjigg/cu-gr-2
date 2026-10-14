// 文件功能：实现特殊线网描述及导线集合读取，供 CUGR 提取固定障碍。
// 【零基础阅读】访问特殊线网原始描述和导线集合，保留输入已有金属路径。
// CUGR Design::read 用这些资料提取线段和过孔上下层障碍。
// 这里返回描述引用，调用方遍历才逐个处理路径，不会自动扣减资源。
/* Copyright 2014-2018 Rsyn
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
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   PhysicalSpecialNet.h
 * Author: jucemar
 *
 * Created on 23 de Maio de 2017, 20:58
 */

namespace Rsyn {
// getNet 返回特殊网的描述资料供遍历 wire/segment/point，与普通逻辑网 allPins 读法不同。

inline const DefNetDscp & PhysicalSpecialNet::getNet() const { return data->clsNet; }

// -----------------------------------------------------------------------------

} // end namespace 
