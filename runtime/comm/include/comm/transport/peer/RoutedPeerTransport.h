#ifndef PILOT_ROUTED_PEER_TRANSPORT_H
#define PILOT_ROUTED_PEER_TRANSPORT_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct RoutedPeerMessage {
    int senderRank{};
    int receiverRank{};
    int tag{};
    int type{};
    std::vector<int64_t> words;
    std::string text;
};

class RoutedPeerTransport {
public:
    using MessageHandler = std::function<void(RoutedPeerMessage)>;

    virtual ~RoutedPeerTransport() = default;

    virtual void init(int rank, MessageHandler handler) = 0;

    virtual void send(const RoutedPeerMessage &message) = 0;

    virtual void finalize() = 0;
};

#endif
