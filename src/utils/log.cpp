// 文件功能：实现计时日志和操作系统相关的内存统计，供导入、布线和结束阶段输出资源信息。
// Linux 当前内存取 /proc/self/statm；峰值取 getrusage，并统一转换为 MB。
// 【白话流程】程序开始建立一个全局计时器，每次 log 输出距起点的秒数。
// Linux 的 statm 给出进程页面数量，乘每页字节数再除 1048576 转成 MB。
// 峰值是“运行期间最多用过多少”，当前值是“现在用了多少”，不是同一个指标。
// #if/#elif 由编译器选平台代码，运行时不会同时执行 Unix 和 Windows 两套。
// ostringstream 先拼时间戳，再写到调用者的输出流，避免改它原有的数字格式。
#include "log.h"

#include <iomanip>
#include <sstream>

#if defined(__unix__)
#include <sys/resource.h>
#include <unistd.h>
#elif defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#endif

namespace utils {

timer::timer() { start(); }
void timer::start() { _start = clock::now(); }
double timer::elapsed() const { return std::chrono::duration<double>(clock::now() - _start).count(); }

timer tstamp;

std::ostream& operator<<(std::ostream& os, const timer& t) {
    std::ostringstream oss;  // seperate the impact of format
    oss << "[" << std::setprecision(3) << std::setw(8) << std::fixed << t.elapsed() << "] ";
    os << oss.str();
    return os;
}

double mem_use::get_current() {
    // Linux：fp 为 /proc/self/statm 读取句柄，rss 为驻留页数，乘页字节数转 MB。
    // Windows：info 系统进程内存结构，WorkingSetSize 为当前工作集字节数。
#if defined(__unix__)
    long rss = 0L;
    FILE* fp = NULL;
    if ((fp = fopen("/proc/self/statm", "r")) == NULL) {
        return 0.0; /* Can't open? */
    }
    if (fscanf(fp, "%*s%ld", &rss) != 1) {
        fclose(fp);
        return 0.0; /* Can't read? */
    }
    fclose(fp);
    return rss * sysconf(_SC_PAGESIZE) / 1048576.0;
#elif defined(_WIN32)
    PROCESS_MEMORY_COUNTERS info;
    GetProcessMemoryInfo(GetCurrentProcess(), &info, sizeof(info));
    return info.WorkingSetSize / 1048576.0;
#else
    return 0.0;  // unknown
#endif
}

double mem_use::get_peak() {
    // rusage 为 Unix 系统资源统计；Windows info 为进程内存记录，峰值不是当前值。
#if defined(__unix__)
    struct rusage rusage;
    getrusage(RUSAGE_SELF, &rusage);
    return rusage.ru_maxrss / 1024.0;
#elif defined(_WIN32)
    PROCESS_MEMORY_COUNTERS info;
    GetProcessMemoryInfo(GetCurrentProcess(), &info, sizeof(info));
    return info.PeakWorkingSetSize / 1048576.0;
#else
    return 0.0;  // unknown
#endif
}

std::ostream& log(std::ostream& os) {
    os << tstamp;
    return os;
}

void logeol(int n) {
    for (int i = 0; i < n; i++) {
        log() << std::endl;
    }
}

void loghline() {
    log() << "- - - - - - - - - -" << std::endl;
}

void logmem() {
    log() << "MEM: cur=" << mem_use::get_current() << "MB, "
          << "peak=" << mem_use::get_peak() << "MB" << std::endl;
}

}  // namespace utils
