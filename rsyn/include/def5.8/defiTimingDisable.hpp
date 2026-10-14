// 文件功能：声明 DEF 时序禁用约束的实例与起终引脚接口。
// 【零基础阅读】时序禁用描述某些实例/引脚关系不参与特定时序分析。
// 这不等于把引脚物理断开，也不等于从路由名单删除线网。
// 本文件声明约束字段接口，实际含义要结合时序使用方而非几何路径判断。
// *****************************************************************************
// from/to 这里为时序约束起终端标识，而非两端几何坐标；禁用时序分析不表示物理断线。
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

#ifndef defiTimingDisable_h
#define defiTimingDisable_h

#include <stdio.h>
#include "defiKRDefs.hpp"

BEGIN_LEFDEF_PARSER_NAMESPACE

// A Timing disable can be a from-to  or a thru or a macro.
//   A macro is either a fromto macro or a thru macro.
class defrData;


class defiTimingDisable {
public:
  defiTimingDisable(defrData *data);
  void Init();

  void Destroy();
  ~defiTimingDisable();

  void clear();

  void setFromTo(const char* fromInst, const char* fromPin,
		 const char* toInst, const char* toPin);
  void setThru(const char* fromInst, const char* fromPin);
  void setMacro(const char* name);
  void setMacroThru(const char* thru);
  void setMacroFromTo(const char* fromPin, const char* toPin);
  void setReentrantPathsFlag();

  int hasMacroThru() const;
  int hasMacroFromTo() const;
  int hasThru() const;
  int hasFromTo() const;
  int hasReentrantPathsFlag() const;

  const char* fromPin() const;
  const char* toPin() const;
  const char* fromInst() const;
  const char* toInst() const;
  const char* macroName() const;
  const char* thruPin() const;    // Also macro thru
  const char* thruInst() const;

  // debug print
  void print(FILE* f) const;

protected:
  char* fromInst_;  // also macro name and thru inst
  int fromInstLength_;
  char* toInst_;
  int toInstLength_;
  char* fromPin_;  // also macro thru and thru pin
  int fromPinLength_;
  char* toPin_;
  int toPinLength_;

  int hasFromTo_;
  int hasThru_;
  int hasMacro_;
  int hasReentrantPathsFlag_;

  defrData *defData;
};


END_LEFDEF_PARSER_NAMESPACE

USE_LEFDEF_PARSER_NAMESPACE

#endif
