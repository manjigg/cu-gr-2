// 文件功能：保存物理布线网格的层、轨道与空间组织数据。
// 【零基础阅读】物理布线网格组织工艺层和轨道相关资料，方便按位置/层访问。
// 它属于 Rsyn 物理数据库；CUGR 的 GridGraph 是另建的容量/需求模型。
// 本文件定义底层记录，decl/impl 同名文件包装成物理网格查询接口。
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
 * File:   PhysicalRoutingGridData.h
 * Author: jucemar
 *
 * Created on February 2, 2018, 9:00 PM
 */

#ifndef PHYSICALROUTINGGRIDDATA_H
#define PHYSICALROUTINGGRIDDATA_H

namespace Rsyn {
// clsTracks 关联轨道组，clsLayer 层，clsSpacing.x/y 两轴 DBU 轨距，clsNumTracks[轴] 轨道数量。
// clsBounds DBU 边界，clsBottomRoutingGrid/clsTopRoutingGrid 相邻金属网格，不是需求账本。

class PhysicalRoutingGridData : public PhysicalObject {
	friend class PhysicalDesign;
public:
	std::vector<Rsyn::PhysicalTracks> clsTracks;
	Rsyn::PhysicalLayer clsLayer;
	DBUxy clsSpacing;
	int clsNumTracks[2];
	Bounds clsBounds;
	Rsyn::PhysicalRoutingGrid clsBottomRoutingGrid;
	Rsyn::PhysicalRoutingGrid clsTopRoutingGrid;
	PhysicalRoutingGridData() = default;
}; // end class

} // end namespace

#endif /* PHYSICALROUTINGGRIDDATA_H */
