// 文件功能：注册内置设计读取器，使 Session 可以按名称创建并执行输入加载流程。
// 【零基础阅读】把读取器名字与创建方式登记到 Session，以便按名字选输入流程。
// 注册只提供可用入口，不代表所有已登记读取器都会立即打开文件。
// 真正 LEF/DEF 读取可追到 ISPD2018Reader 的 load/parsingFlow。
/* Copyright 2014-2018 Rsyn
 * 注册名是字符串键，工厂/创建函数是联系方式；真实文件名到 load 调用时才提供。
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
 
/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

#include <Rsyn/Session>

// Readers
#include "rsyn/io/reader/ISPD2018Reader.h"

// Registration
namespace Rsyn {

void Session::registerReaders() {
	registerReader<Rsyn::ISPD2018Reader>("ispd18");
	registerReader<Rsyn::ISPD2018Reader>("ispd19"); // ispd19 files are the same from ispd18 contest
} // end method
} // end namespace
