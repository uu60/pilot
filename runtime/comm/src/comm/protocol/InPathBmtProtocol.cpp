#include "comm/protocol/InPathBmtProtocol.h"

#include "comm/Comm.h"
#include "comm/security/BmtCrypto.h"
#include "conf/Conf.h"
#include "intermediate/IntermediateDataSupport.h"

#include <stdexcept>

bool InPathBmtProtocol::enabled() {
    return Conf::ENABLE_IN_PATH_BMT_SWITCH;
}

bool InPathBmtProtocol::shouldRouteServerTraffic() {
    return enabled() && Comm::isServerRank(Comm::rank());
}

std::vector<int64_t> InPathBmtProtocol::makeRequest(int tag, int bmtRequestCount,
                                                    const std::vector<int64_t> &payload) {
    PilotPacketRequest packet;
    packet.tag = tag;
    packet.laneId = IntermediateDataSupport::currentLane();
    packet.bmtRequestCount = bmtRequestCount;
    packet.payload = payload;
    return PilotPacket::encodeRequest(packet);
}

PilotPacketRequest InPathBmtProtocol::decodeRequest(const std::vector<int64_t> &request) {
    return PilotPacket::decodeRequest(request);
}

std::vector<int64_t> InPathBmtProtocol::makeEnvelope(
        int srcRank, int dstRank, int tag, int laneId,
        const std::vector<int64_t> &payload,
        const std::vector<EncryptedBitwiseBmt> &trailer) {
    PilotPacketEnvelope packet;
    packet.srcRank = srcRank;
    packet.dstRank = dstRank;
    packet.tag = tag;
    packet.laneId = laneId;
    packet.payload = payload;
    packet.encryptedTrailer = trailer;
    return PilotPacket::encodeEnvelope(packet);
}

std::vector<int64_t> InPathBmtProtocol::unpackPayload(const std::vector<int64_t> &envelope) {
    auto packet = PilotPacket::decodeEnvelope(envelope);
    if (packet.dstRank != Comm::rank()) {
        throw std::runtime_error("Encrypted pilot envelope was delivered to the wrong server.");
    }
    if (!packet.encryptedTrailer.empty()) {
        std::vector<RawBitwiseBmt> trailer;
        trailer.reserve(packet.encryptedTrailer.size());
        for (uint32_t i = 0; i < packet.encryptedTrailer.size(); ++i) {
            trailer.push_back(BmtCrypto::decrypt(packet.encryptedTrailer[i], packet.dstRank,
                                                 packet.laneId, packet.tag, i, packet.payload));
        }
        IntermediateDataSupport::offerRawBitwiseBmts(packet.laneId, trailer);
    }
    return packet.payload;
}
