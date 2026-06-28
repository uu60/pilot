#ifndef PILOT_TCP_SOFTWARE_SWITCH_TRANSPORT_H
#define PILOT_TCP_SOFTWARE_SWITCH_TRANSPORT_H

#include "comm/transport/switch/SwitchFrameTransport.h"

#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

class TcpSoftwareSwitchTransport final : public SwitchFrameTransport {
public:
    void send(const PilotFrame &frame) override;

    PilotFrame receive(int physicalTag) override;

    void finalize() override;

    static void runSwitch();

private:
    void ensureClientConnected();

    void receiveLoop();

    static void sendFrame(int fd, const PilotFrame &frame);

    static bool receiveFrame(int fd, PilotFrame &frame);

    std::mutex _mutex;
    std::condition_variable _cv;
    std::map<int, std::deque<PilotFrame>> _pending;
    std::thread _receiveThread;
    int _socketFd = -1;
    bool _connected = false;
    bool _finalized = false;
};

#endif
