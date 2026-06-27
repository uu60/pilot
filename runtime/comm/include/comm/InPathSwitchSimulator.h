#ifndef IN_PATH_SWITCH_SIMULATOR_H
#define IN_PATH_SWITCH_SIMULATOR_H

#include <cstdint>
#include <vector>

#include "intermediate/item/BitwiseBmt.h"

class InPathSwitchSimulator {
public:
    static constexpr int64_t ENVELOPE_MAGIC = 0x5041525345434550LL; // "PARSECEP" marker
    static constexpr int64_t REQUEST_MAGIC = 0x5041525352455154LL;  // "PARSREQT" marker
    static constexpr int64_t TRAILER_MAGIC = 0x424d5454524c524cLL;  // "BMTTRLRL" marker
    static constexpr int64_t SHUTDOWN_MAGIC = 0x5357485f53544f50LL; // "SWH_STOP" marker
    static constexpr int CONTROL_TAG = 0x3ffffffe;

    static bool enabled();

    static bool isSwitchRank();

    static bool shouldRouteServerTraffic();

    static void run();

    static void sendShutdown();

    static std::vector<int64_t> makeSwitchRequest(int tag, int bmtRequestCount,
                                                  const std::vector<int64_t> &payload);

    static void unpackSwitchRequest(const std::vector<int64_t> &request,
                                    int &laneId,
                                    int &bmtRequestCount,
                                    std::vector<int64_t> &payload);

    static std::vector<int64_t> makeEnvelope(int srcRank, int dstRank, int tag, int laneId,
                                             const std::vector<int64_t> &payload,
                                             const std::vector<RawBitwiseBmt> &trailer);

    static std::vector<int64_t> forwardRequest(int srcRank, int tag,
                                               const std::vector<int64_t> &request);

    static std::vector<int64_t> unpackPayload(const std::vector<int64_t> &envelope);
};

#endif
