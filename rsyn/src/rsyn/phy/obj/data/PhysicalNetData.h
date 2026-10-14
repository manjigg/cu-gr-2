// 文件功能：保存物理线网的引脚边界与布线数据。
// 【零基础阅读】逻辑 Net 是端点名单，PhysicalNet 保存空间边界和路径相关资料。
// 包围盒是盖住相关引脚的大矩形，不等于已经选定的导线轨迹。
// 实际路径由布线数据表达；本文件只是这些物理资料的底层存储。
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
 * File:   PhysicalNetData.h
 * Author: jucemar
 *
 * Created on 15 de Setembro de 2016, 19:15
 */

#ifndef RSYN_PHYSICAL_NET_DATA_H
#define RSYN_PHYSICAL_NET_DATA_H

namespace Rsyn {
// clsBounds 网端点 DBU 包围盒，clsNet 逻辑网，clsRouting 已有物理路径。
// clsBoundPins[轴][低/高] 为决定边界的引脚；两下标不表示两个空间格子坐标。

class PhysicalNetData {
public:
	Bounds clsBounds;
	Rsyn::Net clsNet;
	Rsyn::Pin clsBoundPins[2][2];
	PhysicalRouting clsRouting;

	PhysicalNetData() {
		clsBoundPins[LOWER][X] = nullptr;
		clsBoundPins[LOWER][Y] = nullptr;
		clsBoundPins[UPPER][X] = nullptr;
		clsBoundPins[UPPER][Y] = nullptr;
	} // end constructor 
}; // end class

} // end class 

#endif
