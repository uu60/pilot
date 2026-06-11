#ifndef PILOT_LANE_THREAD_POOL_H
#define PILOT_LANE_THREAD_POOL_H

#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

class LaneThreadPool {
public:
    static void init(int laneNum);
    static void finalize();

    static int laneNum();
    static int currentLane();
    static void bindLane(int laneId);
    static bool hasBoundLane();

    template<typename F>
    static auto submit(int laneId, F &&fn) -> std::future<decltype(fn())> {
        using R = decltype(fn());
        auto task = std::make_shared<std::packaged_task<R()>>(
            [laneId, f = std::forward<F>(fn)]() mutable {
                bindLane(laneId);
                return f();
            });
        auto future = task->get_future();
        enqueue(laneId, [task]() { (*task)(); });
        return future;
    }

private:
    struct Worker {
        std::mutex mutex;
        std::condition_variable cv;
        std::queue<std::function<void()>> tasks;
        std::thread thread;
        bool stop = false;
    };

    inline static std::vector<std::unique_ptr<Worker>> _workers;
    inline static thread_local int _currentLane = -1;

    static void enqueue(int laneId, std::function<void()> task);
    static void runWorker(int laneId);
};

#endif
