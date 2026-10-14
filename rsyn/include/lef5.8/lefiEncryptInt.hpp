// 文件功能：声明 LEF 加密输入处理相关接口，供底层读取器处理受保护文本。
// 【零基础阅读】这层接口支持 LEF 的受保护/加密文本处理，是读取链的一部分。
// 加密文本处理与工艺对象构建不同，先得到可解析输入才可读取层/macro。
// 本文件仅给底层函数约定，不包含 CUGR 的布线算法或加密策略实现。
// *****************************************************************************
// 缓冲/文件参数服务受保护文本解读，长度为输入字节数而非 DBU；具体加密实现位于底层库。
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

#ifndef DEFI_ENCRYPTINT_H
#define DEFI_ENCRYPTINT_H

#include <stdio.h>
#include "lefiKRDefs.hpp"

BEGIN_LEFDEF_PARSER_NAMESPACE

// Unable the reader to read encrypted lef file.
//  This function must be called before lefrRead().
// /
extern void lefrEnableReadEncrypted();

END_LEFDEF_PARSER_NAMESPACE

USE_LEFDEF_PARSER_NAMESPACE

#endif
