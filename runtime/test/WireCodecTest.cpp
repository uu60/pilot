#include "comm/protocol/EthernetFrameCodec.h"

#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

int main() {
    const PilotMessage original{0, 1, 1234, 2, {7, -8, 9}, {}};
    auto frame = EthernetFrameCodec::encode(original);

    PilotMessage decoded;
    assert(EthernetFrameCodec::decode(frame.data(), frame.size(), decoded));
    assert(decoded.senderRank == original.senderRank);
    assert(decoded.receiverRank == original.receiverRank);
    assert(decoded.tag == original.tag);
    assert(decoded.type == original.type);
    assert(decoded.words == original.words);

    frame[12] = 0;
    frame[13] = 0;
    assert(!EthernetFrameCodec::decode(frame.data(), frame.size(), decoded));
    return 0;
}
