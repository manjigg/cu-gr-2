// 文件功能：声明 LEF 压缩文件读取适配接口，连接解析器与压缩输入流。
// 【零基础阅读】压缩输入适配先取得解压后的文本流，再交给 LEF 语法读取器。
// 文件压缩与库单元几何是不同层次，不能用压缩流位置当芯片坐标。
// 这里只声明入口，调用方选择普通/压缩读法后由底层库完成操作。
// *****************************************************************************
// 流/缓冲参数保存压缩文件的解码输入，字节数量与工艺物理单位完全不同。
// *****************************************************************************
// Copyright 2012, Cadence Design Systems
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

#ifndef LEFZLIB_H

typedef void* lefGZFile;

/* 
 * Name: lefGZipOpen
 * Description: Open a gzip file
 * Returns: A file pointer
 */
extern lefGZFile lefGZipOpen(const char* gzipFile, const char* mode);

/* 
 * Name: lefGZipClose
 * Description: Close a gzip file
 * Returns: 0 if no errors
 */
extern int lefGZipClose(lefGZFile filePtr);

/*
 * Name: lefrReadGZip
 * Description: Parse a lef gzip file
 * Returns: 0 if no errors
 */
extern int lefrReadGZip(lefGZFile file, const char* gzipFile, void* uData);

#endif
