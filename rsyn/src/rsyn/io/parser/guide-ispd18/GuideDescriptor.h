// 文件功能：定义 guide 文件中线网及分层矩形的中间描述结构。
// 【零基础阅读】guide 临时表保存线网名和一组层名/矩形，尚未关联正式逻辑线网。
// 解析器先读文字，RoutingGuide 服务再按名字找到 Net/Layer 并保存。
// 一个网有多个区域，这些是允许布线走廊，不是最终精确金属路径点。
/* Copyright 2014-2018 Rsyn
 * 网名定位逻辑 Net，层名定位工艺层，四坐标定位 DBU 矩形；一网保存多区域而非一条零宽线。
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
 
#ifndef ISPD18GUIDEDESCRIPTOR_H
#define ISPD18GUIDEDESCRIPTOR_H

#include <deque>
#include "rsyn/util/Bounds.h"

class GuideLayerDscp {
public:
	Bounds clsLayerGuide;
	std::string clsLayer = "";
	GuideLayerDscp() = default;
}; // end class 

// -----------------------------------------------------------------------------

class GuideNetDscp {
public:
	std::string clsNetName = "";
	std::deque<GuideLayerDscp> clsLayerDscps;
	GuideNetDscp() = default;
}; // end class 

// -----------------------------------------------------------------------------

class GuideDscp {
public:
	std::deque<GuideNetDscp> clsNetGuides;
	GuideDscp() = default;
}; // end class 

// -----------------------------------------------------------------------------

#endif /* ISPD18GUIDEDESCRIPTOR_H */
