// 文件功能：定义 DEF 库跨编译器/平台的函数声明及兼容性宏。
// 【零基础阅读】兼容宏让函数声明适应不同编译器/语言环境。
// #define/#if 在编译时展开/选择，不能逐行当作芯片布线运行步骤。
// 初学者先看使用宏后的函数参数用途，再按需理解底层兼容写法。
// *****************************************************************************
// 本文件主要是预处理兼容定义，无独立运行时变量；宏的参数是编译期替换项。
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

#ifndef defiKRDEFS_h
#define defiKRDEFS_h

#define BEGIN_LEFDEF_PARSER_NAMESPACE namespace LefDefParser {
#define END_LEFDEF_PARSER_NAMESPACE }
#define USE_LEFDEF_PARSER_NAMESPACE using namespace LefDefParser;

#endif /* defiKRDEFS_h */
