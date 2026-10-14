// 文件功能：声明 DEF 楼层规划约束（FPC）及相关方向/成员属性接口。
// 【零基础阅读】FPC 保存布局相关约束及参与对象，像摆放零件的排列规则。
// 方向/成员说明约束关系，不是最终布线边或线网需求值。
// 此处声明解析对象接口，具体支持字段由方法列表及其 has/get 标志说明。
// *****************************************************************************
// 约束记录变量：成员名描述参与实例，方向标志描述布局排列，数值阈值属于该约束。
// 顺序/数量下标不是物理坐标；方法只读写本记录，不在此求可制造的导线。
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

#ifndef defiFPC_h
#define defiFPC_h

#include <stdio.h>
#include "defiKRDefs.hpp"

BEGIN_LEFDEF_PARSER_NAMESPACE

class defrData;

class defiFPC {
public:
  defiFPC(defrData *data);
  void Init();

  void Destroy();
  ~defiFPC();

  void clear();

  void setName(const char* name, const char* direction);
  void setAlign();
  void setMax(double num);
  void setMin(double num);
  void setEqual(double num);
  void setDoingBottomLeft();
  void setDoingTopRight();
  void addRow(const char* name);
  void addComps(const char* name);
  void addItem(char typ, const char* name);

  const char* name() const;
  int isVertical() const;
  int isHorizontal() const;
  int hasAlign() const;
  int hasMax() const;
  int hasMin() const;
  int hasEqual() const;
  double alignMax() const;
  double alignMin() const;
  double equal() const;

  int numParts() const;

  // Return the constraint number "index" where index is
  //    from 0 to numParts()
  // The returned corner is 'B' for bottom left  'T' for topright
  // The returned typ is 'R' for rows   'C' for comps
  // The returned char* points to name of the item.
  void getPart(int index, int* corner, int* typ, char** name) const;

  // debug print
  void print(FILE* f) const;

protected:
  char* name_;
  int nameLength_;
  char direction_; // H or V
  char hasAlign_;
  char hasMin_;
  char hasMax_;
  char hasEqual_;
  char corner_;   // Bottomleft or Topright
  double minMaxEqual_;
  int namesAllocated_;  // allocated size of names_ and rowOrComp_
  int namesUsed_;       // number of entries used in the arrays
  char* rowOrComp_;
  char** names_;

  defrData *defData;
};


END_LEFDEF_PARSER_NAMESPACE

USE_LEFDEF_PARSER_NAMESPACE

#endif
