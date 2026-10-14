// 文件功能：声明障碍工艺层和矩形集合接口。
// 【零基础阅读】查询障碍在哪层、由哪些矩形组成，用于识别不能随意布线的位置。
// allBounds 是多个边界的集合，一个障碍对象不必只有一个矩形。
// 真正间距扩张和容量扣减由使用方完成，查询形状本身不更新路由资源。
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
 * File:   PhysicalObstacle.h
 * Author: jucemar
 *
 * Created on 13 de Setembro de 2016, 20:08
 */

#ifndef PHYSICALDESIGN_PHYSICALOBSTACLE_H
#define PHYSICALDESIGN_PHYSICALOBSTACLE_H

namespace Rsyn {
// clsLayer 障碍层，clsBounds[i] 第 i 个 DBU 矩形；i 是列表下标，矩形坐标另存 Bounds 内。

class PhysicalObstacle : public Proxy < PhysicalObstacleData > { 
	friend class PhysicalDesign;
	friend class PhysicalDesignData;
protected:
	//! @brief Constructs a Rsyn::PhysicalObstacle object with a pointer to Rsyn::PhysicalObstacleData.
	PhysicalObstacle(PhysicalObstacleData * data) : Proxy(data) {}
public:
	//! @brief Constructs a Rsyn::PhysicalObstacle object with a null pointer to Rsyn::PhysicalObstacleData.
	PhysicalObstacle() : Proxy(nullptr) {}
	//! @brief Constructs a Rsyn::PhysicalObstacle object with a null pointer to Rsyn::PhysicalObstacleData.
	PhysicalObstacle(std::nullptr_t) : Proxy(nullptr) {}
	
	//! @brief Returns the Rsyn::PhysicalLayer object related to the current Rsyn::PhysicalObstacle.
	Rsyn::PhysicalLayer getLayer() const;
	//! @brief Returns the vector of rectangular Bounds in the Rsyn::PhysicalObstacle.
	const std::vector<Bounds> & allBounds() const;
	//! @brief Returns the total number of rectangular Bounds in the Rsyn::PhysicalObstacle.
	std::size_t getNumObs() const;
	//! @brief Returns true if Rsyn::PhysicalObstacle has a Rsyn::PhysicalLayer object defined. 
	//! Otherwise, returns false.
	bool hasLayer() const;
}; // end class 

} // end namespace 

#endif /* PHYSICALDESIGN_PHYSICALOBSTACLE_H */
