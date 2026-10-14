// 文件功能：声明 DEF 解析库的调试开关及诊断输出辅助接口。
// 【零基础阅读】调试接口控制解析过程输出多少诊断信息，帮助定位文件读入问题。
// 函数原型只约定参数与返回类型，调用才会触发其内部实现。
// 不同于路由阶段日志，本模块属于 DEF 格式读取层。
// *****************************************************************************
// 参数范围：调试标志控制库输出等级，消息文字传输出流；它们不是算法线长/层编号。
// *****************************************************************************
// Copyright 2013, Cadence Design Systems
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
//  $Author: dell $
//  $Revision: #1 $
//  $Date: 2017/06/06 $
//  $State:  $
// *****************************************************************************
// *****************************************************************************

#ifndef defiDebug_h
#define defiDebug_h 

#include "defiKRDefs.hpp"

BEGIN_LEFDEF_PARSER_NAMESPACE

class defrData;

/* Set flag */
extern void defiSetDebug (int num, int value) ;

/* Read flag */
extern int defiDebug (int num) ;

/* Error loggin function */
extern void defiError(int check, int msgNum, const char* message, defrData *defData = NULL);

/* for auto upshifting names in case insensitive files */
extern const char* upperCase(const char* c, defrData *defData = NULL);
extern const char* DEFCASE(const char* ch, defrData *defData = NULL);

END_LEFDEF_PARSER_NAMESPACE

USE_LEFDEF_PARSER_NAMESPACE

#endif
