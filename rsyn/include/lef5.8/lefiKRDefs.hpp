// 文件功能：定义 LEF 库跨编译器/平台的函数声明兼容宏。
// 【零基础阅读】兼容宏让库声明能在不同 C/C++ 编译环境中使用。
// 预处理选择发生在编译时，不会在芯片布线过程中按 if 一样逐段执行。
// 宏只是简写/适配，理解函数实际输入输出仍要看展开后的参数类型。
// *****************************************************************************
// 主要是编译兼容宏，无独立芯片数据变量；宏参数是展开占位，不能按运行函数调用顺序读。
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

#ifndef lefiKRDEFS_h
#define lefiKRDEFS_h

#define BEGIN_LEFDEF_PARSER_NAMESPACE namespace LefDefParser {
#define END_LEFDEF_PARSER_NAMESPACE }
#define USE_LEFDEF_PARSER_NAMESPACE using namespace LefDefParser;

#endif
