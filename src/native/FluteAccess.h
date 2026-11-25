#pragma once
#include <mutex>

namespace native3d {
// LUT 在启动线程初始化。库内核仍串行访问，工作线程只并行处理外围候选。
inline std::mutex& fluteMutex() {
    static std::mutex lock;
    return lock;
}
}
