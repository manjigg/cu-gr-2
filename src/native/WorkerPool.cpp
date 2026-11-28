#include "WorkerPool.h"
#include <stdexcept>

namespace native3d {
WorkerPool::WorkerPool(unsigned count) {
    if(count<1 || count>128) throw std::invalid_argument("Invalid worker count");
    threads.reserve(count);
    try {while(threads.size()<count) threads.emplace_back(&WorkerPool::work,this,threads.size());}
    catch(...) {shutdown();throw;}
}
void WorkerPool::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        closing=true;
        cancel();
    }
    ready.notify_all();
    for(auto& worker:threads) worker.join();
}
WorkerPool::~WorkerPool() {shutdown();}
size_t WorkerPool::run(size_t count,const std::function<void(size_t,size_t)>& task) {
    if(!count) return 0;
    std::unique_lock<std::mutex> lock(mutex);
    if(operation) throw std::logic_error("Overlapping worker batches");
    stopBatch.store(false,std::memory_order_relaxed);
    failure=nullptr;
    next=0;
    total=count;
    remaining=threads.size();
    operation=&task;
    ++epoch;
    ready.notify_all();
    finished.wait(lock,[&]{return remaining==0;});
    operation=nullptr;
    if(failure) std::rethrow_exception(failure);
    return next;
}
void WorkerPool::work(size_t worker) {
    size_t last=0;
    std::unique_lock<std::mutex> lock(mutex);
    while(true) {
        ready.wait(lock,[&]{return closing || epoch!=last;});
        if(closing) return;
        last=epoch;
        while(next<total && !stopBatch.load(std::memory_order_relaxed)) {
            const size_t id=next++;
            const auto& runTask=*operation;
            lock.unlock();
            try {runTask(id,worker);}
            catch(...) {
                lock.lock();
                if(!failure) failure=std::current_exception();
                cancel();
                break;
            }
            lock.lock();
        }
        if(--remaining==0) finished.notify_one();
    }
}
}
