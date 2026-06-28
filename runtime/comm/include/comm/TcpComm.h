#ifndef PILOT_TCP_COMM_H
#define PILOT_TCP_COMM_H

#include "comm/MpiComm.h"
#include "comm/transport/SwitchFrameTransport.h"

#include <vector>

class TcpComm : public MpiComm {
public:
    static bool enabled();

    static void runSwitch();

    static void sendShutdown();

protected:
    void serverSendImpl_(const int64_t &source, int width, int tag) override;

    void serverSendImpl_(const std::vector<int64_t> &source, int width, int tag) override;

    void serverReceiveImpl_(int64_t &source, int width, int tag) override;

    void serverReceiveImpl_(std::vector<int64_t> &source, int width, int tag) override;

    AbstractRequest *serverSendAsyncImpl_(const int64_t &source, int width, int tag) override;

    AbstractRequest *serverSendAsyncImpl_(const std::vector<int64_t> &source, int width, int tag) override;

    AbstractRequest *serverReceiveAsyncImpl_(int64_t &target, int width, int tag) override;

    AbstractRequest *serverReceiveAsyncImpl_(std::vector<int64_t> &target, int count, int width, int tag) override;

    void finalize_() override;

private:
    static SwitchFrameTransport &transport();

    static void send(int physicalTag, const std::vector<int64_t> &request);

    static std::vector<int64_t> receive(int physicalTag);
};

#endif
