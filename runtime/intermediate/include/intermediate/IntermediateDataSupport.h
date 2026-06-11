#ifndef PILOT_INTERMEDIATE_DATA_SUPPORT_H
#define PILOT_INTERMEDIATE_DATA_SUPPORT_H

#include "intermediate/item/BitwiseBmt.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <vector>

class IntermediateDataSupport {
private:
    inline static std::mutex _mutex;
    inline static std::condition_variable _cv;
    inline static std::vector<std::deque<RawBitwiseBmt>> _bitwiseBmtBundles;
    inline static std::vector<int> _bitwiseBmtRequestCounts;

public:
    static void init();

    static int currentLane();

    static void requestBitwiseBmts(int count);

    static int consumeBitwiseBmtRequestCount();

    static void offerRawBitwiseBmts(int lane, const std::vector<RawBitwiseBmt> &bmts);

    static std::vector<BitwiseBmt> pollBitwiseBmts(int count, int width);

    static size_t bitwiseBmtSize();
};

#endif
