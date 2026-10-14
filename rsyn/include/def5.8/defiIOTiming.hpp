// 文件功能：声明 DEF 输入/输出时序描述的参数查询和设置接口。
// 【零基础阅读】I/O 时序记录描述端口相关延迟等时间要求，不是几何位置。
// has* 表示文件是否提供某参数，get* 取值，解析器用 set* 填入。
// 当前全局路线主流程主要读几何/连接，接口存在不代表所有时序字段都参与评分。
// *****************************************************************************
// rise/fall 参数区分上升/下降沿时序，min/max 区分范围；数值是时间/电气量而非 DBU。
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

#ifndef defiIOTiming_h
#define defiIOTiming_h

#include <stdio.h>
#include "defiKRDefs.hpp"

BEGIN_LEFDEF_PARSER_NAMESPACE

class defrData;

class defiIOTiming {
public:
  defiIOTiming(defrData *data);
  void Init();

  void Destroy();
  ~defiIOTiming();

  void clear();

  void setName(const char* inst, const char* pin);
  void setVariable(const char* riseFall, double min, double max);
  void setSlewRate(const char* riseFall, double min, double max);
  void setCapacitance(double num);
  void setDriveCell(const char* name);
  void setFrom(const char* name);
  void setTo(const char* name);
  void setParallel(double num);


  int hasVariableRise() const;
  int hasVariableFall() const;
  int hasSlewRise() const;
  int hasSlewFall() const;
  int hasCapacitance() const;
  int hasDriveCell() const;
  int hasFrom() const;
  int hasTo() const;
  int hasParallel() const;

  const char* inst() const;
  const char* pin() const;
  double variableFallMin() const;
  double variableRiseMin() const;
  double variableFallMax() const;
  double variableRiseMax() const;
  double slewFallMin() const;
  double slewRiseMin() const;
  double slewFallMax() const;
  double slewRiseMax() const;
  double capacitance() const;
  const char* driveCell() const;
  const char* from() const;
  const char* to() const;
  double parallel() const;

  // debug print
  void print(FILE* f) const;

protected:
  char* inst_;
  int instLength_;
  char* pin_;
  int pinLength_;
  char* from_;
  int fromLength_;
  char* to_;
  int toLength_;
  char* driveCell_;
  char driveCellLength_;
  char hasVariableRise_;
  char hasVariableFall_;
  char hasSlewRise_;
  char hasSlewFall_;
  char hasCapacitance_;
  char hasDriveCell_;
  char hasFrom_;
  char hasTo_;
  char hasParallel_;
  double variableFallMin_;
  double variableRiseMin_;
  double variableFallMax_;
  double variableRiseMax_;
  double slewFallMin_;
  double slewRiseMin_;
  double slewFallMax_;
  double slewRiseMax_;
  double capacitance_;
  double parallel_;

  defrData *defData;
};


END_LEFDEF_PARSER_NAMESPACE

USE_LEFDEF_PARSER_NAMESPACE

#endif
