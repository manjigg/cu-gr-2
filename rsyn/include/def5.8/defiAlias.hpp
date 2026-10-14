// 文件功能：声明 DEF 别名迭代接口，供底层解析库访问别名表。
// 【零基础阅读】别名把另一种名字关联到解析器名称，迭代器用于逐项查看这张表。
// 本头文件声明访问约定，具体方法在 libdef.a 中；它不负责决定导线路线。
// 看方法返回类型区分名字、值和“是否还有下一项”，不要把声明顺序当执行顺序。
// *****************************************************************************
// 变量范围：别名表的遍历位置只定位名字记录；字符串参数是名字/值，不能当坐标或对象 id。
// *****************************************************************************
// Copyright 2013 - 2014, Cadence Design Systems
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

#ifndef defiALIAS_h
#define defiALIAS_h

#include "defiKRDefs.hpp"

BEGIN_LEFDEF_PARSER_NAMESPACE

class defAliasIterator;
class defrData;

class defiAlias_itr {
public:
  defiAlias_itr(defrData *defData = 0);
  void Init();

  void Destroy();
  ~defiAlias_itr();

  int Next();
  const char* Key();
  const char* Data();
  int Marked();

protected:
  defAliasIterator   *iterator;
  int first;
  defrData *defData;
};

END_LEFDEF_PARSER_NAMESPACE

USE_LEFDEF_PARSER_NAMESPACE

#endif
