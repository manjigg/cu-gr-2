#include "native/WorkerPool.h"
#include <stdexcept>
#include <atomic>
#include <set>

using namespace native3d;
int main() {
    WorkerPool pool(2);
    std::mutex mutex;
    std::condition_variable cv;
    std::set<size_t> ids;
    pool.run(2,[&](size_t,size_t worker) {
        std::unique_lock<std::mutex> lock(mutex);
        ids.insert(worker);
        cv.notify_all();
        cv.wait(lock,[&]{return ids.size()==2;});
    });
    std::atomic<int> completed{0};
    bool failed=false;
    try {pool.run(8,[](size_t,size_t){throw std::runtime_error("injected task failure");});}
    catch(const std::runtime_error&) {failed=true;}
    if(!failed) return 1;
    if(pool.run(100,[&](size_t,size_t){pool.cancel();})>2) return 1;
    if(pool.run(30,[&](size_t,size_t){++completed;})!=30 || completed!=30) return 1;
}
