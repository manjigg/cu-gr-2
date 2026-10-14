// 文件功能：声明 LEF 库调试设置和诊断输出接口。
// 【零基础阅读】调试助手控制 LEF 读取诊断和库内部排错输出。
// #define/函数原型是编译约定，调用对应函数才执行操作。
// 读取错误应结合解析日志看，不要把它与布线阶段拥塞报告混成同一问题。
// *****************************************************************************
// 调试参数控制诊断等级/文字，计数用于读库错误统计，不参与实际路径代价函数。
// *****************************************************************************
// Copyright 2012 - 2013, Cadence Design Systems
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

#ifndef lefiDebug_h
#define lefiDebug_h 1

#include "lefiKRDefs.hpp"

BEGIN_LEFDEF_PARSER_NAMESPACE

// Set flag 
extern void lefiSetDebug (int num, int value) ;

// Read flag 
extern int lefiDebug (int num) ;

// Error reporting routine 
extern void lefiError (int check, int msgNum, const char* msg);

extern const char* CASE(const char *x); 

END_LEFDEF_PARSER_NAMESPACE

USE_LEFDEF_PARSER_NAMESPACE

#endif
