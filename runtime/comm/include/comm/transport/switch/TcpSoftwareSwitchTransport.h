#ifndef PILOT_TCP_SOFTWARE_SWITCH_TRANSPORT_H
#define PILOT_TCP_SOFTWARE_SWITCH_TRANSPORT_H

#include "comm/transport/peer/RoutedPeerTransport.h"

#include <mutex>
#include <thread>

class TcpSoftwareSwitchTransport final : public RoutedPeerTransport {
public:
    void init(int rank, MessageHandler handler) override;

    void send(const RoutedPeerMessage &message) override;

    void finalize() override;

    static void runSwitch();

private:
    void receiveLoop();

    int _rank = 0;
    int _socketFd = -1;
    MessageHandler _handler;
    std::mutex _sendMutex;
    std::thread _receiveThread;
    bool _finalized = false;
};

#endif
