#ifndef PILOT_TCP_COMM_H
#define PILOT_TCP_COMM_H

#include "comm/MpiComm.h"

#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

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
    static void send(int physicalTag, const std::vector<int64_t> &request);

    static std::vector<int64_t> receive(int physicalTag);

    static void ensureClientConnected();

    static void receiveLoop();

    static void finalizeTcp();

    static void sendFrame(int fd, int sourceRank, int physicalTag, const std::vector<int64_t> &words);

    static bool receiveFrame(int fd, int &sourceRank, int &physicalTag, std::vector<int64_t> &words);

    inline static std::mutex _mutex;
    inline static std::condition_variable _cv;
    inline static std::map<int, std::deque<std::vector<int64_t>>> _pending;
    inline static std::thread _receiveThread;
    inline static int _socketFd = -1;
    inline static bool _connected = false;
    inline static bool _finalized = false;
};

#endif
