#pragma once
#include <atomic>
#include <condition_variable>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace native3d {
// 固定工作线程按需领取网任务，run 的返回点是读快照与串行写账本之间的屏障。
class WorkerPool {
public:
    explicit WorkerPool(unsigned count);
    ~WorkerPool();
    WorkerPool(const WorkerPool&)=delete;
    WorkerPool& operator=(const WorkerPool&)=delete;
    size_t run(size_t count,const std::function<void(size_t,size_t)>& task);
    void cancel() {stopBatch.store(true,std::memory_order_relaxed);}
    const std::atomic<bool>& cancellation() const {return stopBatch;}
    size_t size() const {return threads.size();}
private:
    void work(size_t worker);
    void shutdown();
    std::vector<std::thread> threads;
    std::mutex mutex;
    std::condition_variable ready,finished;
    const std::function<void(size_t,size_t)>* operation=nullptr;
    size_t epoch=0,next=0,total=0,remaining=0;
    bool closing=false;
    std::atomic<bool> stopBatch{false};
    std::exception_ptr failure;
};
}
