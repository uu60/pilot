#ifndef PILOT_TAP_ROUTED_PEER_TRANSPORT_H
#define PILOT_TAP_ROUTED_PEER_TRANSPORT_H

#include "comm/transport/peer/RoutedPeerTransport.h"

class TapRoutedPeerTransport : public RoutedPeerTransport {
public:
    void init(int rank, MessageHandler handler) override;

    void send(const RoutedPeerMessage &message) override;

    void finalize() override;
};

#endif
