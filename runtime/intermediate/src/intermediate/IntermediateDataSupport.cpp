#include "intermediate/IntermediateDataSupport.h"

#include "conf/Conf.h"
#include "parallel/LaneThreadPool.h"
#include "utils/Math.h"

#include <algorithm>
#include <stdexcept>

void IntermediateDataSupport::init() {
    std::lock_guard<std::mutex> lock(_mutex);
    const int laneNum = std::max(1, Conf::IN_PATH_LANE_NUM);
    _bitwiseBmtBundles.clear();
    _bitwiseBmtBundles.resize(laneNum);
    _bitwiseBmtRequestCounts.assign(laneNum, 0);
}

int IntermediateDataSupport::currentLane() {
    const int lane = LaneThreadPool::currentLane();
    if (lane < 0 || lane >= static_cast<int>(_bitwiseBmtBundles.size())) {
        throw std::runtime_error("Current BMT lane is not initialized.");
    }
    return lane;
}

void IntermediateDataSupport::requestBitwiseBmts(int count) {
    if (count < 0) {
        throw std::runtime_error("Invalid in-path BMT request count.");
    }
    if (Conf::IN_PATH_BMT_BUNDLE_SIZE > 0 && count > Conf::IN_PATH_BMT_BUNDLE_SIZE) {
        throw std::runtime_error("In-path BMT request exceeds the fixed per-lane bundle size.");
    }

    std::lock_guard<std::mutex> lock(_mutex);
    _bitwiseBmtRequestCounts[currentLane()] = count;
}

int IntermediateDataSupport::consumeBitwiseBmtRequestCount() {
    std::lock_guard<std::mutex> lock(_mutex);
    const int lane = currentLane();
    const int count = _bitwiseBmtRequestCounts[lane];
    _bitwiseBmtRequestCounts[lane] = 0;
    return count;
}

void IntermediateDataSupport::offerRawBitwiseBmts(int lane, const std::vector<RawBitwiseBmt> &bmts) {
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_bitwiseBmtBundles.empty()) {
            const int laneNum = std::max(1, Conf::IN_PATH_LANE_NUM);
            _bitwiseBmtBundles.resize(laneNum);
            _bitwiseBmtRequestCounts.assign(laneNum, 0);
        }
        if (lane < 0 || lane >= static_cast<int>(_bitwiseBmtBundles.size())) {
            throw std::runtime_error("Invalid in-path BMT lane id.");
        }

        auto &queue = _bitwiseBmtBundles[lane];
        for (const auto &bmt: bmts) {
            queue.push_back(bmt);
        }
    }
    _cv.notify_all();
}

std::vector<BitwiseBmt> IntermediateDataSupport::pollBitwiseBmts(int count, int width) {
    if (count < 0) {
        throw std::runtime_error("Invalid in-path BMT poll count.");
    }
    if (Conf::IN_PATH_BMT_BUNDLE_SIZE > 0 && count > Conf::IN_PATH_BMT_BUNDLE_SIZE) {
        throw std::runtime_error("In-path BMT request exceeds the fixed per-lane bundle size.");
    }

    requestBitwiseBmts(count);

    std::unique_lock<std::mutex> lock(_mutex);
    if (_bitwiseBmtBundles.empty()) {
        const int laneNum = std::max(1, Conf::IN_PATH_LANE_NUM);
        _bitwiseBmtBundles.resize(laneNum);
        _bitwiseBmtRequestCounts.assign(laneNum, 0);
    }
    const int lane = currentLane();
    _cv.wait(lock, [&] { return static_cast<int>(_bitwiseBmtBundles[lane].size()) >= count; });

    auto &queue = _bitwiseBmtBundles[lane];
    std::vector<BitwiseBmt> result;
    result.reserve(count);
    for (int i = 0; i < count; ++i) {
        const auto raw = queue.front();
        queue.pop_front();
        result.push_back(BitwiseBmt{raw.a, raw.b, raw.c});
    }
    return result;
}

size_t IntermediateDataSupport::bitwiseBmtSize() {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_bitwiseBmtBundles.empty()) {
        return 0;
    }
    return _bitwiseBmtBundles[currentLane()].size();
}
