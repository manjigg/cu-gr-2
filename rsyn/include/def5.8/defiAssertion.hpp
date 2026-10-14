// 文件功能：声明 DEF 断言/约束对象，保存相关表达式和约束属性。
// 【零基础阅读】断言/约束对象保存 DEF 中对设计关系的要求，而非程序运行的 assert。
// set/add 方法用于解析时填资料，get/has 方法用于读回字段/判断可选项。
// 本文件提供库接口，实际解析回调由 DEFControlParser 注册并转换成项目描述。
// *****************************************************************************
// 参数逻辑：端点/实例文字标明约束对象，计数标明表达式项数，has* 标明输入可选项。
// add/set 填解析记录，查询编号只在本约束对象内有效，不是全芯片下标。
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

#ifndef defiAssertion_h
#define defiAssertion_h

#include "defiKRDefs.hpp"
#include <stdio.h>

BEGIN_LEFDEF_PARSER_NAMESPACE

// Struct holds the data for one assertion/constraint.
// An assertion or constraint is either a net/path rule or a
// wired logic rule.
//
//  A net/path rule is an item or list of items plus specifications.
//    The specifications are: rise/fall min/max.
//    The items are a list of (one or more) net names or paths or a
//    combination of both.
//
//  A wired logic rule is a netname and a distance.
//
//  We will NOT allow the mixing of wired logic rules and net/path delays
//  in the same assertion/constraint.
//
//  We will allow the rule to be a sum of sums (which will be interpreted
//  as just one list).
//
class defrData;

class defiAssertion {
public:
  defiAssertion(defrData *data);
  void Init();

  void Destroy();
  ~defiAssertion();

  void setConstraintMode();
  void setAssertionMode();
  void setSum();
  void setDiff();
  void setNetName(const char* name);
  void setRiseMin(double num);
  void setRiseMax(double num);
  void setFallMin(double num);
  void setFallMax(double num);
  void setDelay();
  void setWiredlogicMode();
  void setWiredlogic(const char* net, double dist);
  void addNet(const char* name);
  void addPath(const char* fromInst, const char* fromPin,
               const char* toInst, const char* toPin);
  void bumpItems();
  void unsetSum();

  int isAssertion() const;  // Either isAssertion or isConstraint is true
  int isConstraint() const;
  int isWiredlogic() const; // Either isWiredlogic or isDelay is true
  int isDelay() const;
  int isSum() const;
  int isDiff() const;
  int hasRiseMin() const;
  int hasRiseMax() const;
  int hasFallMin() const;
  int hasFallMax() const;
  double riseMin() const;
  double riseMax() const;
  double fallMin() const;
  double fallMax() const;
  const char* netName() const; // Wired logic net name
  double distance() const; // Wired logic distance
  int numItems() const;  // number of paths or nets 
  int isPath(int index) const;   // is item #index a path?
  int isNet(int index) const;    // is item #index a net?
  void path(int index, char** fromInst, char** fromPin,
	   char** toInst, char** toPin) const; // Get path data for item #index
  void net(int index, char** netName) const;   // Get net data for item #index

  void clear();
  void print(FILE* f) const;


protected:
  char isAssertion_;
  char isSum_;
  char isDiff_;
  char hasRiseMin_;
  char hasRiseMax_;
  char hasFallMin_;
  char hasFallMax_;
  char isWiredlogic_;
  char isDelay_;
  char* netName_;     // wired logic net name
  int netNameLength_;
  double riseMin_;
  double riseMax_;
  double fallMin_;
  double fallMax_;    // also used to store the wired logic dist
  int numItems_;
  int numItemsAllocated_;
  char* itemTypes_;
  int** items_;       // not really integers.

  defrData *defData;
};

END_LEFDEF_PARSER_NAMESPACE

USE_LEFDEF_PARSER_NAMESPACE

#endif
