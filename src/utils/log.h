// 文件功能：声明计时器、当前/峰值内存查询和带时间戳的流式日志、可变参数打印接口。
// 【白话职责】日志让你看到程序进行到哪里，计时/内存让你看到资源用了多少。
// timer 保存开始时刻，elapsed 返回已经过了几秒；它不负责调度路由阶段。
// mem_use 的 static 方法不需要先创建对象，直接 mem_use::get_current() 即可。
// print(T,TAIL...) 用模板接收不同类型，逐个打印再递归处理剩余参数。
// 文件只声明系统内存查询，Linux/Windows 的实际做法在 log.cpp。
//
// Some logging utilities
// 1. "log() << ..." will show a time stamp first
// 2. "print(a, b, c)" is python-like print for any a, b, c that has operator<< overloaded. For example,
//      int a = 10;
//      double b = 3.14;
//      std::string c = "Gengjie";
//      print(a, b, c);
//     This code piece will show "10 3.14 Gengjie".
//

#pragma once

#include <chrono>
#include <iostream>
#include <string>

namespace utils {

// 1. Timer

class timer {
    // clock 时钟类型别名，_start 开始时刻；elapsed 返回现在减 _start 的秒数。
    using clock = std::chrono::high_resolution_clock;

private:
    clock::time_point _start;

public:
    timer();
    void start();
    double elapsed() const;  // seconds
};

std::ostream& operator<<(std::ostream& os, const timer& t);

// 2. Memory

class mem_use {
public:
    static double get_current();  // MB
    static double get_peak();     // MB
};

// 3. Easy print

// print(a, b, c)
inline void print() { std::cout << std::endl; }
template <typename T, typename... TAIL>
void print(const T& t, TAIL... tail) {
    // t 当前要打印项，tail 其余项；每次输出一项再递归，空 print() 换行停止递归。
    std::cout << t << ' ';
    print(tail...);
}

// "log() << a << b << c" puts a time stamp in beginning
std::ostream& log(std::ostream& os = std::cout);

// "printlog(a, b, c)" puts a time stamp in beginning
template <typename... T>
void printlog(T... t) {
    log();
    print(t...);
}

template <typename... T>
void printflog(T... t) {
    log();
    printf(t...);
}

void logeol(int n = 1);
void loghline();
void logmem();

}  // namespace utils
