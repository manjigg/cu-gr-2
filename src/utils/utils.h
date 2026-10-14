// 文件功能：统一导出几何、日志、容器打印和增强枚举工具，作为 global.h 的公共基础。
// 【白话职责】这是工具总目录，方便调用方一次引入常用工具。
// 这里没有 main，也没有读取设计或选路径；#include 只是让定义对编译器可见。
// 不懂几何看 geo.h，不懂时间/内存日志看 log.h/log.cpp，不懂容器打印看 prettyprint.h。
#pragma once
// 此聚合头没有自己的变量/函数体，几何 x/y/low/high 看 geo.h，日志时间/内存看 log.h/.cpp。

#include "geo.h"
#include "log.h"
#include "prettyprint.h"
#include "enum.h"
