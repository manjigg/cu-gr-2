// 文件功能：声明 ISPD guide 文本解析接口，输出线网/层/矩形描述对象。
// 【零基础阅读】解析接口输入文件路径，输出按线网组织的 guide 描述。
// 类内部读取状态和方法将名字/括号块/矩形区分，细节在 .cpp。
// 读取已有 guide 与 CUGR 生成新 guide 是相反的数据流，别混成同一阶段。
/* Copyright 2014-2018 Rsyn
 * 文件路径是输入，GuideDscp 为输出；每条区域的数字先转换为坐标，文字层名后续再查对象。
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
 
#ifndef ISPD18GUIDEPARSER_H
#define	ISPD18GUIDEPARSER_H
#include <fstream>
#include <vector>
#include "GuideDescriptor.h"
#include "rsyn/util/Bounds.h"
/*net1230
(
95010 71819 100710 91201 Metal2
95010 83220 100710 91201 Metal1
89310 71819 100710 77520 Metal3
95010 71819 100710 77520 Metal2
89310 71819 95010 77520 Metal2
89310 71819 95010 77520 Metal2
89310 71819 95010 77520 Metal1
)
 */

class GuideParser {
protected:
	 std::ifstream clsIS;
public:
	GuideParser() = default;
	void parse(std::string & guidePath, GuideDscp & guideDscp);
	
protected:
	bool readLine(std::vector<std::string>& tokens);
	bool readNet(std::string & net);
	bool isStartLayer(std::string & token);
	bool isEndLayer(std::string & token);
	bool readLayerGuide(GuideNetDscp & dscp);
	
};




#endif /* ISPD18GUIDEPARSER_H */
