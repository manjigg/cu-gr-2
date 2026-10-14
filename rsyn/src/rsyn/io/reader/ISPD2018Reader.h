// 文件功能：声明 ISPD 设计读取流程，管理 LEF/DEF/guide 文件及对应描述对象。
// 【零基础阅读】本读取器保存输入路径和临时描述，组织 LEF -> DEF -> 数据库填充顺序。
// load 是外部入口，parse* 专门处理对应格式，populateDesign 建正式逻辑/物理对象。
// 类型名带 2018，但当前 CUGR 也借此读取所支持算例的 LEF/DEF。
/* Copyright 2014-2018 Rsyn
 * lefFile/defFile/guideFile 为路径成员；lefDescriptor/defDescriptor 是中间描述，不是磁盘文件内容字符串。
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
 
#ifndef RSYN_ISPD2018_READER_H
#define RSYN_ISPD2018_READER_H

#include <Rsyn/Session>

namespace Rsyn  {
	
class RoutingGuide;
	
class ISPD2018Reader : public Reader {
public:
	ISPD2018Reader() = default;
	virtual bool load(const Rsyn::Json& params) override;
	
private:
	Session session;
	
	std::string lefFile;
	std::string defFile;
	std::string guideFile;
	LefDscp lefDescriptor;
	DefDscp defDescriptor;
	RoutingGuide *routingGuide;
	
	void parsingFlow();
	void parseLEFFile();
	void parseDEFFile();
	void parseGuideFile();
	void populateDesign();
};

}

#endif /* ISPD2018READER_H */
