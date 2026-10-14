// 文件功能：声明 DEF 区域对象的矩形边界、区域类型和附加属性接口。
// 【零基础阅读】区域记录名字、类别与一个或多个边界矩形，可作为放置约束引用。
// 大包围盒不一定等于实际区域，多个矩形形状需按集合理解。
// 本解析对象保存文件资料，后续设计导入才建立实例到区域的关联。
// *****************************************************************************
// 区域 name 为文字，矩形 low/high x/y 为 DBU 边界，类型为约束类别；列表序号不是金属层。
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

#ifndef defiRegion_h
#define defiRegion_h

#include "defiKRDefs.hpp"
#include <stdio.h>

BEGIN_LEFDEF_PARSER_NAMESPACE

class defrData;

// Struct holds the data for one property.
class defiRegion {
public:
  defiRegion(defrData *data);
  void Init();

  void Destroy();
  ~defiRegion();

  void clear();
  void setup(const char* name);
  void addRect(int xl, int yl, int xh, int yh);
  void addProperty(const char* name, const char* value, const char type);
  void addNumProperty(const char* name, const double d,
                      const char* value, const char type);
  void setType(const char* type);         // 5.4.1

  const char* name() const;

  int numProps() const;
  const char*  propName(int index) const;
  const char*  propValue(int index) const;
  double propNumber(int index) const;
  const char   propType(int index) const;
  int propIsNumber(int index) const;
  int propIsString(int index) const;

  int hasType() const;                    // 5.4.1
  const char* type() const;               // 5.4.1

  int numRectangles() const;
  int xl(int index) const;
  int yl(int index) const;
  int xh(int index) const;
  int yh(int index) const;

  void print(FILE* f) const;

protected:
  char* name_;
  int nameLength_;

  int numRectangles_;
  int rectanglesAllocated_;
  int* xl_;
  int* yl_;
  int* xh_;
  int* yh_;

  int numProps_;
  int propsAllocated_;
  char**  propNames_;
  char**  propValues_;
  double* propDValues_;
  char*   propTypes_;

  char* type_;

  defrData *defData;
};



END_LEFDEF_PARSER_NAMESPACE

USE_LEFDEF_PARSER_NAMESPACE

#endif
