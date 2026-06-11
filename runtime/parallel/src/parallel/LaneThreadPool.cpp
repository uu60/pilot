#include "parallel/LaneThreadPool.h"

#include <algorithm>
#include <stdexcept>

void LaneThreadPool::init(int laneNum) {
    finalize();
    const int n = std::max(1, laneNum);
    _workers.reserve(n);
    for (int lane = 0; lane < n; ++lane) {
        _workers.push_back(std::make_unique<Worker>());
    }
    for (int lane = 0; lane < n; ++lane) {
        _workers[lane]->thread = std::thread([lane]() { runWorker(lane); });
    }
}

void LaneThreadPool::finalize() {
    for (auto &worker: _workers) {
        {
            std::lock_guard<std::mutex> lock(worker->mutex);
            worker->stop = true;
        }
        worker->cv.notify_all();
    }
    for (auto &worker: _workers) {
        if (worker->thread.joinable()) {
            worker->thread.join();
        }
    }
    _workers.clear();
}

int LaneThreadPool::laneNum() {
    return static_cast<int>(_workers.size());
}

int LaneThreadPool::currentLane() {
    if (_currentLane < 0) {
        throw std::runtime_error("Current thread is not bound to a BMT lane.");
    }
    return _currentLane;
}

void LaneThreadPool::bindLane(int laneId) {
    if (laneId < 0) {
        throw std::runtime_error("Invalid BMT lane id.");
    }
    _currentLane = laneId;
}

bool LaneThreadPool::hasBoundLane() {
    return _currentLane >= 0;
}

void LaneThreadPool::enqueue(int laneId, std::function<void()> task) {
    if (laneId < 0 || laneId >= static_cast<int>(_workers.size())) {
        throw std::runtime_error("Invalid BMT lane id for lane worker submission.");
    }
    auto &worker = *_workers[laneId];
    {
        std::lock_guard<std::mutex> lock(worker.mutex);
        worker.tasks.push(std::move(task));
    }
    worker.cv.notify_one();
}

void LaneThreadPool::runWorker(int laneId) {
    bindLane(laneId);
    auto &worker = *_workers[laneId];
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(worker.mutex);
            worker.cv.wait(lock, [&] { return worker.stop || !worker.tasks.empty(); });
            if (worker.stop && worker.tasks.empty()) {
                return;
            }
            task = std::move(worker.tasks.front());
            worker.tasks.pop();
        }
        task();
    }
}
