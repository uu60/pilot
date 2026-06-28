#ifndef PILOT_TCP_ROUTED_PEER_TRANSPORT_H
#define PILOT_TCP_ROUTED_PEER_TRANSPORT_H

#include "comm/transport/peer/RoutedPeerTransport.h"

#include <thread>

class TcpRoutedPeerTransport : public RoutedPeerTransport {
public:
    void init(int rank, MessageHandler handler) override;

    void send(const RoutedPeerMessage &message) override;

    void finalize() override;

private:
    void listenerLoop();

    int _rank = 0;
    int _listenFd = -1;
    MessageHandler _handler;
    std::thread _listenerThread;
    bool _finalized = false;
};

#endif
