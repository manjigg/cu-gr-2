// 文件功能：声明 LEF 解析配置、错误处理与属性类型管理设置。
// 【零基础阅读】读取设置控制 LEF 文本解析方式、错误处理和属性类型登记。
// 这与 Parameters 中布线权重是两个层次：一个读输入，一个比较路线。
// 配置值/回调/工作状态不要混看，具体注册步骤在 LEFControlParser 中。
// *****************************************************************************
// 开关/版本/警告上限管理文本读取，属性类型表管理名字解释；不是路由迭代次数或代价权重。
// *****************************************************************************
// Copyright 2012 - 2017, Cadence Design Systems
// 
// This  file  is  part  of  the  Cadence  LEF/DEF  Open   Source
// Distribution,  Product Version 5.8. 
// 
// Licensed under the Apache License, Version 2.0 (the "License");
//    you may not use this file except in compliance with the License.
//    You may obtain a copy of the License at
// 
//        http://www.apache.org/licenses/LICENSE-2.0
// 
//    Unless required by applicable law or agreed to in writing, software
//    distributed under the License is distributed on an "AS IS" BASIS,
//    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
//    implied. See the License for the specific language governing
//    permissions and limitations under the License.
// 
// For updates, support, or to become part of the LEF/DEF Community,
// check www.openeda.org for details.
// 
//  $Author: arakhman $
//  $Revision: #11 $
//  $Date: 2013/04/23 $
//  $State:  $
// *****************************************************************************
// *****************************************************************************

#ifndef lefrSettings_h
#define lefrSettings_h

#include <cstring>
#include <string>
#include <map>
#include <set>

#include "lefrReader.hpp"

BEGIN_LEFDEF_PARSER_NAMESPACE

struct lefCompareCStrings 
{
    bool operator()(const char* lhs, const char* rhs) const {
        return std::strcmp(lhs, rhs) < 0;
    }
};

typedef std::map<const char*, int, lefCompareCStrings>  lefKeywordMap;
typedef std::map<int, int>                              MsgsDisableMap;

typedef std::set<std::string>               StringSet;

class lefrProps {
public:
    lefiPropType  lefrCompProp; 
    lefiPropType  lefrLayerProp; 
    lefiPropType  lefrLibProp; 
    lefiPropType  lefrMacroProp; 
    lefiPropType  lefrNondefProp; 
    lefiPropType  lefrPinProp; 
    lefiPropType  lefrViaProp; 
    lefiPropType  lefrViaRuleProp; 
};

class lefrSettings {
public:
                    lefrSettings();

    void            init_symbol_table();
    static void     reset();
    void            addLef58Type(const char *lef58Type, 
                                 const char **layerType);

    std::string     getLayerLef58Types(const char *type) const;

    void            disableMsg(int msgId);
    void            enableMsg(int msgId);
    void            enableAllMsgs();
    int             suppresMsg(int msgId);

    static std::string     getToken(const std::string   &input, 
                                    int                 &startIdx);

    lefKeywordMap       Keyword_set; 

    char CommentChar;
    double VersionNum;
    int DisPropStrProcess;
    int CaseSensitive;
    int CaseSensitiveSet;
    int DeltaNumberLines;
    int AntennaInoutWarnings;
    int AntennaInputWarnings;
    int AntennaOutputWarnings;
    int ArrayWarnings;
    int CaseSensitiveWarnings;
    int CorrectionTableWarnings;
    int DielectricWarnings;
    int EdgeRateScaleFactorWarnings;
    int EdgeRateThreshold1Warnings;
    int EdgeRateThreshold2Warnings;
    int IRDropWarnings;
    int InoutAntennaWarnings;
    int InputAntennaWarnings;
    LEFI_LINE_NUMBER_FUNCTION LineNumberFunction;
    int LayerWarnings;
    int MacroWarnings;
    int MaxStackViaWarnings;
    int MinFeatureWarnings;
    int NoWireExtensionWarnings;
    int NoiseMarginWarnings;
    int NoiseTableWarnings;
    int NonDefaultWarnings;
    int OutputAntennaWarnings;
    int PinWarnings;
    LEFI_READ_FUNCTION ReadFunction;
    int ReadEncrypted; 
    int RegisterUnused;
    int RelaxMode;
    int ShiftCase;
    int SiteWarnings;
    int SpacingWarnings;
    int TimingWarnings;
    int UnitsWarnings;
    int UseMinSpacingWarnings;
    int ViaRuleWarnings;
    int ViaWarnings;
    int LogFileAppend;
    int TotalMsgLimit;
    lefiUserData UserData;

    StringSet Lef58TypePairs;

    LEFI_MALLOC_FUNCTION MallocFunction;
    LEFI_REALLOC_FUNCTION ReallocFunction;
    LEFI_FREE_FUNCTION FreeFunction;
    LEFI_LOG_FUNCTION ErrorLogFunction;
    LEFI_LOG_FUNCTION SetLogFunction;
    LEFI_WARNING_LOG_FUNCTION WarningLogFunction;

    int MsgLimit[MAX_LEF_MSGS];

    MsgsDisableMap msgsDisableMap;
    int  dAllMsgs; 

    lefrProps lefProps;
};

extern lefrSettings* lefSettings;

END_LEFDEF_PARSER_NAMESPACE

#endif
