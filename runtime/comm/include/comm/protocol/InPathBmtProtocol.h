#ifndef PILOT_IN_PATH_BMT_PROTOCOL_H
#define PILOT_IN_PATH_BMT_PROTOCOL_H

#include "comm/protocol/PilotPacket.h"

#include <cstdint>
#include <vector>

class InPathBmtProtocol {
public:
    static bool enabled();

    static bool shouldRouteServerTraffic();

    static std::vector<int64_t> makeRequest(int tag, int bmtRequestCount,
                                            const std::vector<int64_t> &payload);

    static PilotPacketRequest decodeRequest(const std::vector<int64_t> &request);

    static std::vector<int64_t> makeEnvelope(int srcRank, int dstRank, int tag, int laneId,
                                             const std::vector<int64_t> &payload,
                                             const std::vector<EncryptedBitwiseBmt> &trailer);

    static std::vector<int64_t> unpackPayload(const std::vector<int64_t> &envelope);
};

#endif
