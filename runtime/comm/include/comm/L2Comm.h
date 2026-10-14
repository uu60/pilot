#ifndef PILOT_L2_COMM_H
#define PILOT_L2_COMM_H

#include "comm/Comm.h"
#include "comm/message/MessageInbox.h"
#include "comm/protocol/PilotMessage.h"

#include <mutex>
#include <string>
#include <thread>

class L2Comm final : public Comm {
public:
    int rank_() override;
    void init_(int argc, char **argv) override;
    void finalize_() override;
    bool isServer_() override;
    bool isClient_() override;

    void send_(const std::vector<int64_t> &source, int width, int receiverRank, int tag) override;
    void send_(int64_t source, int width, int receiverRank, int tag) override;
    void send_(const std::string &source, int receiverRank, int tag) override;
    void receive_(int64_t &source, int width, int senderRank, int tag) override;
    void receive_(std::vector<int64_t> &source, int width, int senderRank, int tag) override;
    void receive_(std::string &target, int senderRank, int tag) override;

    AbstractRequest *sendAsync_(const std::vector<int64_t> &source, int width, int receiverRank, int tag) override;
    AbstractRequest *sendAsync_(const int64_t &source, int width, int receiverRank, int tag) override;
    AbstractRequest *sendAsync_(const std::string &source, int receiverRank, int tag) override;
    AbstractRequest *receiveAsync_(int64_t &target, int width, int senderRank, int tag) override;
    AbstractRequest *receiveAsync_(std::vector<int64_t> &target, int count, int width,
                                   int senderRank, int tag) override;
    AbstractRequest *receiveAsync_(std::string &target, int length, int senderRank, int tag) override;

protected:
    void serverSendImpl_(const int64_t &source, int width, int tag) override;
    void serverSendImpl_(const std::vector<int64_t> &source, int width, int tag) override;
    void serverReceiveImpl_(int64_t &source, int width, int tag) override;
    void serverReceiveImpl_(std::vector<int64_t> &source, int width, int tag) override;
    AbstractRequest *serverSendAsyncImpl_(const int64_t &source, int width, int tag) override;
    AbstractRequest *serverSendAsyncImpl_(const std::vector<int64_t> &source, int width, int tag) override;
    AbstractRequest *serverReceiveAsyncImpl_(int64_t &target, int width, int tag) override;
    AbstractRequest *serverReceiveAsyncImpl_(std::vector<int64_t> &target, int count,
                                             int width, int tag) override;

private:
    enum MessageType { CONTROL_MESSAGE = 0, INT64_MESSAGE = 1, VECTOR_MESSAGE = 2, STRING_MESSAGE = 3 };

    void sendMessage(const PilotMessage &message);
    void receiveLoop();

    int _rank = 0;
    int _fd = -1;
    std::string _interfaceName;
    MessageInbox _inbox;
    std::mutex _sendMutex;
    std::thread _receiveThread;
    bool _finalized = false;
};

#endif
