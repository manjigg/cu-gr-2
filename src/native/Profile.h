#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace native3d {
class MultiDieDesign;
class RoutingGraph;
class Measured;
class MeasuredNet;
struct Route;

enum class Timing {
    Total, Input, Initialization, Seed, LocalSeed, CrossDieSeed, Flute, PatternDP,
    HbtSearch, BranchRepair, Maze, Available, Resources, Cost, Capacity, StepCost,
    Validate, Commit, Release, Stage1, Stage2, Stage3, Candidates, Master, Output, SeedMaze, Stage3Maze, MazeRefine, Conflict, Count
};
enum class Event {
    LocalPaths, HbtSites, LabelsGenerated, LabelMerges, LabelsPruned, MazeExpansions,
    MasterNodes, MazeRetries, MazeFailed, MazeTimeouts, RefineTimeouts, HbtOccupied, HbtSpacing, Count
};
enum class NetMethod { Seed, Pattern, Detour, Maze, Count };

class Profile {
public:
    using Clock = std::chrono::steady_clock;
    Profile();
    ~Profile();
    Profile(const Profile&) = delete;
    Profile& operator=(const Profile&) = delete;
    static Profile* current() noexcept { return active; }
    static void count(Event event, uint64_t amount=1);
    static void labels(size_t live);
#ifdef CUGR_DP_MEMORY_PROFILE
    void changeLabelBytes(size_t previous,size_t replacement) noexcept;
    size_t liveLabelBytes() const noexcept {return labelBytes;}
    size_t peakLiveLabelBytes() const noexcept {return peakLabelBytes;}
#endif
    static void retry();
    // 每个工作任务独立计时，仅在结束时归并，主线程墙钟与工作线程时间分栏。
    static void worker(Profile* parent,int net,NetMethod method,const std::function<void()>& task);
    void prepareNets(size_t count);
    void describeNets(const MultiDieDesign&, const RoutingGraph&, const std::vector<Route>&);
    void write(const std::string& directory) const;
private:
    friend class Measured;
    friend class MeasuredNet;
    // 对齐连续计时桶，保证工作线程归并时的双字 SIMD 读取对齐。
    struct alignas(16) Times {
        uint64_t calls=0;
        Clock::duration inclusive{}, exclusive{};
    };
    struct NetTiming {
        std::array<Clock::duration,static_cast<int>(NetMethod::Count)> time{};
        uint64_t retries=0;
    };
    std::array<Times,static_cast<int>(Timing::Count)> times{};
    std::array<uint64_t,static_cast<int>(Event::Count)> events{};
    std::vector<NetTiming> nets;
    bool workerLocal=false;
    NetTiming workerNet;
    std::mutex mergeMutex;
    decltype(times) workerTimes{};
    decltype(events) workerEvents{};
    std::map<int,NetTiming> workerNets;
    size_t workerPeakLabels=0;
#ifdef CUGR_DP_MEMORY_PROFILE
    size_t labelBytes=0,peakLabelBytes=0,workerPeakLabelBytes=0;
#endif
    NetTiming& netTiming(int id) {return workerLocal?workerNet:nets.at(id);}
    struct NetRecord {
        int id;
        std::array<std::string,2> names;
        std::array<int64_t,4> box;
        size_t pins, hbt;
        unsigned dieMask;
    };
    std::vector<NetRecord> slow;
    int currentNet=-1;
    size_t peakLabels=0;
    Measured* frame=nullptr;
    MeasuredNet* netFrame=nullptr;
    static thread_local Profile* active;
};

class Measured {
public:
    explicit Measured(Timing kind);
    ~Measured();
    Measured(const Measured&) = delete;
    Measured& operator=(const Measured&) = delete;
private:
    Timing kind;
    Profile* profile;
    Measured* previous=nullptr;
    Profile::Clock::time_point start;
    Profile::Clock::duration children{};
};

class MeasuredNet {
public:
    MeasuredNet(int net,NetMethod method);
    ~MeasuredNet();
    MeasuredNet(const MeasuredNet&) = delete;
    MeasuredNet& operator=(const MeasuredNet&) = delete;
private:
    Profile* profile;
    int net, previous=-1;
    NetMethod method;
    MeasuredNet* parent=nullptr;
    Profile::Clock::time_point start;
    Profile::Clock::duration children{};
};
}
