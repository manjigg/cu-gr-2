// 文件功能：保存库障碍的工艺层和矩形集合。
// 【零基础阅读】Obstacle 表示已有金属或禁止穿越区域，按层保存多个矩形。
// 同样平面坐标在不同层有不同意义，所以障碍形状必须连同层引用保存。
// CUGR 后面将其扩张并扣减轨道容量；此处只保存原始几何。
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
 * File:   PhysicalObstacleData.h
 * Author: jucemar
 *
 * Created on 13 de Setembro de 2016, 20:02
 */

#ifndef PHYSICALDESIGN_PHYSICALOBSTACLEDATA_H
#define PHYSICALDESIGN_PHYSICALOBSTACLEDATA_H


namespace Rsyn {
// clsLayer 工艺层，clsBounds[i] 第 i 块 DBU 障碍；模板障碍使用时还需实例位移/朝向变换。

class PhysicalObstacleData : public PhysicalObject {
public:
	Rsyn::PhysicalLayer clsLayer;
	std::vector<Bounds> clsBounds;
	PhysicalObstacleData() = default;
}; // end class 

} // end namespace 

#endif /* PHYSICALDESIGN_PHYSICALOBSTACLEDATA_H */
