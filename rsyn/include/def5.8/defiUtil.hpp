// 文件功能：声明 DEF 库内存/字符串和诊断等公共辅助函数。
// 【零基础阅读】解析库公共助手处理字符串、内存和错误/诊断等基础工作。
// char* 是字符串/缓冲区地址，分配与释放约定应按库接口配套使用。
// 这些工具支撑读取对象，不负责 CUGR 的代价评分和路径搜索。
// *****************************************************************************
// 工具参数主要是文字缓冲地址、字节容量和输出消息；size 若指缓冲区，不是芯片尺寸。
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

#ifndef defiUtil_h
#define defiUtil_h

#include "defiKRDefs.hpp"

BEGIN_LEFDEF_PARSER_NAMESPACE

/* Return codes Orient and Rotation */
#define DEF_ORIENT_N  0
#define DEF_ORIENT_W  1
#define DEF_ORIENT_S  2
#define DEF_ORIENT_E  3
#define DEF_ORIENT_FN 4
#define DEF_ORIENT_FW 5
#define DEF_ORIENT_FS 6
#define DEF_ORIENT_FE 7

const char* defiOrientStr(int orient);

END_LEFDEF_PARSER_NAMESPACE

#endif
