// 文件功能：保存按工艺层组织的过孔管理数据。
// 【零基础阅读】过孔把不同金属层接通；管理器按层保存相关数据，减少反复全表查找。
// 此文件是管理器的资料柜，decl 定义怎么查询，impl 实现如何取出结果。
// 金属层编号与过孔编号是不同索引，关联数据负责把它们对应起来。
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
 * File:   PhysicalLayerViaManagerData.h
 * Author: jucemar
 *
 * Created on February 3, 2018, 9:17 AM
 */

#ifndef PHYSICALLAYERVIAMANAGERDATA_H
#define PHYSICALLAYERVIAMANAGERDATA_H

namespace Rsyn {
// clsVias[层] 相关过孔列表，clsBottomVias/clsTopVias[层] 按过孔底/顶金属关联分组。
// key 是层代理，value 是过孔代理列表，不是“整数下标->单一过孔”。

class LayerViaManagerData : public PhysicalObject {
public:
	std::map<Rsyn::PhysicalLayer, std::vector<Rsyn::PhysicalVia>>  clsVias;
	std::map<Rsyn::PhysicalLayer, std::vector<Rsyn::PhysicalVia>>  clsBottomVias;
	std::map<Rsyn::PhysicalLayer, std::vector<Rsyn::PhysicalVia>>  clsTopVias;
	LayerViaManagerData() = default;
}; // end class 

} // end namespace 


#endif /* PHYSICALLAYERVIAMANAGERDATA_H */
