// 文件功能：声明 DEF 实例分组、成员和区域约束接口。
// 【零基础阅读】分组把多实例归到一组，并可关联区域或属性约束。
// 组名、成员名和区域名是不同字段，解析后需按名字建立对象关联。
// 本菜单提供添加/查询操作，保存分组不等于已经移动或布好成员实例。
// *****************************************************************************
// 名字参数区分组名、成员实例名、区域名；矩形参数是约束范围，属性参数是附加文字/值。
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

#ifndef defiGroup_h
#define defiGroup_h

#include <stdio.h>
#include "defiKRDefs.hpp"

BEGIN_LEFDEF_PARSER_NAMESPACE

class defrData;

// Struct holds the data for one property.

class defiGroup {
public:
  defiGroup(defrData *data);
  void Init();

  void Destroy();
  ~defiGroup();

  void clear();

  void setup(const char* name);
  void addProperty(const char* name, const char* value, const char type);
  void addNumProperty(const char* name, const double d,
                      const char* value, const char type);
  void addRegionRect(int xl, int yl, int xh, int yh);
  void setRegionName(const char* name);
  void setMaxX(int x);
  void setMaxY(int y);
  void setPerim(int p);

  const char* name() const;
  const char* regionName() const;
  int hasRegionBox() const;
  int hasRegionName() const;
  int hasMaxX() const;
  int hasMaxY() const;
  int hasPerim() const;
  void regionRects(int* size, int** xl, int**yl, int** xh, int** yh) const;
  int maxX() const;
  int maxY() const;
  int perim() const;

  int numProps() const;
  const char*  propName(int index) const;
  const char*  propValue(int index) const;
  double propNumber(int index) const;
  const char   propType(int index) const;
  int    propIsNumber(int index) const;
  int    propIsString(int index) const;

  // debug print
  void print(FILE* f) const;

protected:
  char* name_;
  int nameLength_;
  char* region_;
  int regionLength_;

  int rectsAllocated_;
  int numRects_;
  int* xl_;
  int* yl_;
  int* xh_;
  int* yh_;

  int maxX_;
  int maxY_;
  int perim_;
  char hasRegionBox_;
  char hasRegionName_;
  char hasPerim_;
  char hasMaxX_;
  char hasMaxY_;

  int numProps_;
  int propsAllocated_;
  char**  propNames_;
  char**  propValues_;
  double* propDValues_;
  char*   propTypes_;

  defrData *defData;
};


END_LEFDEF_PARSER_NAMESPACE

USE_LEFDEF_PARSER_NAMESPACE

#endif
