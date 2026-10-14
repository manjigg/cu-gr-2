// 文件功能：定义线网路由 guide 的分层矩形表示，供 RoutingGuide 服务保存和查询。
// 【零基础阅读】Guide 保存线网可用的分层矩形区域，矩形表示走廊而非细线中心。
// 同一网可有多块区域，过孔接入也可在多个层提供相同平面位置。
// 类型组织已有区域，加载与按网查询由 RoutingGuide 服务提供。
/* Copyright 2014-2018 Rsyn
 * guide 区域含层与矩形；一个区域下标仅定位本网列表，不等于整个设计线网编号。
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
 * File:   Guide.h
 * Author: jucemar
 *
 * Created on 21 de Dezembro de 2016, 17:58
 */

#ifndef ISPD18_GUIDE
#define ISPD18_GUIDE

#include <vector>
#include <Rsyn/PhysicalDesign>

namespace Rsyn {

class LayerGuide {
	friend class RoutingGuide;
	friend class NetGuide;
protected:
	Rsyn::PhysicalLayer  clsPhLayer;
	Bounds clsBounds;
        int clsId;
public:
	LayerGuide() = default;
	const Bounds & getBounds() const { 
		return clsBounds;
	} // end method 
	Rsyn::PhysicalLayer getLayer() const {
		return clsPhLayer;
	} // end method 
        int getId() const 
        {
            return clsId;
        }
        void setBounds(Bounds bounds) {
            clsBounds = bounds;
        } // end method 
        void setLayer(Rsyn::PhysicalLayer layer) {
            clsPhLayer = layer;
        } // end method 
        void setId(int id)
        {
            clsId = id;
        }//end method 
}; // end class

class NetGuide {
	friend class RoutingGuide;
protected:
	std::vector<LayerGuide> clsLayerGuides;
public:
	NetGuide() = default;
        const LayerGuide& getGuide(int id) const { 
            if(id > clsLayerGuides.size())
            {
                std::cout << "Invalid access to net guide";
                std::cout << "layer guide vector size is: " << clsLayerGuides.size()
                        << ", index is: " << id << std::endl;
                getchar();
            }//
            return clsLayerGuides[id];
	} // end method 
	const std::vector<LayerGuide> & allLayerGuides() const { 
		return clsLayerGuides;
	} // end method 
        
        void setLayerGuides(std::vector<LayerGuide> guides) {
            clsLayerGuides = guides;
        }
}; // end class 

} // end namespace 


#endif /* ISPD18_ROUTINGGUIDE */
