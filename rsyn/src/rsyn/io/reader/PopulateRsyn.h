// 文件功能：声明中间描述到 Rsyn 逻辑库、单元、端口和线网的填充接口。
// 【零基础阅读】填充器把临时描述变成可查询库/实例/线网对象。
// 描述里用名字引用，正式网表里需建立对象关联，所以创建顺序有意义。
// 本头文件说明支持哪些输入描述，实际创建和连接过程在 .cpp。
/* Copyright 2014-2018 Rsyn
 * 输入描述保存名字/记录，design 是待填数据库；创建库模板后才能按型号创建实例及端点。
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
 * File:   PopulateRsyn.h
 * Author: jucemar
 *
 * Created on 18 de Fevereiro de 2017, 15:21
 */

#ifndef RSYN_POPULATERSYN_H
#define RSYN_POPULATERSYN_H
#include "rsyn/phy/util/DefDescriptors.h"
#include "rsyn/phy/util/LefDescriptors.h"
#include "rsyn/core/Rsyn.h"
#include "rsyn/io/legacy/PlacerInternals.h"
#include "rsyn/io/legacy/Legacy.h"

namespace Rsyn {

class PopulateRsyn {
public:

	PopulateRsyn() = default;
	PopulateRsyn(const PopulateRsyn& orig) = default;
	virtual ~PopulateRsyn() = default;
protected:
	virtual void populateRsynLibraryFromLef(
		const LefDscp &lefDscp,
		Rsyn::Design rsynDesign);

	virtual void populateRsynNetlistFromVerilog(
		const Legacy::Design &verilogDesign,
		Rsyn::Design rsynDesign);

	virtual void populateRsyn(
		const LefDscp &lefDscp,
		const DefDscp &defDscp,
		const Legacy::Design &verilogDesign,
		Rsyn::Design rsynDesign);

	virtual void populateRsyn(
		const LefDscp &lefDscp,
		const DefDscp &defDscp,
		Rsyn::Design rsynDesign);

};

} // end namespace 
#endif /* RSYN_POPULATERSYN_H */
