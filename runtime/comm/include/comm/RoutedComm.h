#ifndef PILOT_ROUTED_COMM_H
#define PILOT_ROUTED_COMM_H

#include "comm/Comm.h"
#include "comm/transport/peer/RoutedPeerTransport.h"
#include "comm/transport/switch/TcpSoftwareSwitchTransport.h"

#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class RoutedComm : public Comm {
public:
    static bool enabled();

    static void runSwitch();

    static void sendShutdown();

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

    AbstractRequest *receiveAsync_(std::vector<int64_t> &target, int count, int width, int senderRank, int tag) override;

    AbstractRequest *receiveAsync_(std::string &target, int length, int senderRank, int tag) override;

protected:
    void serverSendImpl_(const int64_t &source, int width, int tag) override;

    void serverSendImpl_(const std::vector<int64_t> &source, int width, int tag) override;

    void serverReceiveImpl_(int64_t &source, int width, int tag) override;

    void serverReceiveImpl_(std::vector<int64_t> &source, int width, int tag) override;

    AbstractRequest *serverSendAsyncImpl_(const int64_t &source, int width, int tag) override;

    AbstractRequest *serverSendAsyncImpl_(const std::vector<int64_t> &source, int width, int tag) override;

    AbstractRequest *serverReceiveAsyncImpl_(int64_t &target, int width, int tag) override;

    AbstractRequest *serverReceiveAsyncImpl_(std::vector<int64_t> &target, int count, int width, int tag) override;

private:
    enum MessageType {
        INT64_MESSAGE = 1,
        VECTOR_MESSAGE = 2,
        STRING_MESSAGE = 3
    };

    struct MessageKey {
        int senderRank{};
        int tag{};
        int type{};

        bool operator<(const MessageKey &other) const;
    };

    struct Message {
        std::vector<int64_t> words;
        std::string text;
    };

    void enqueueMessage(int senderRank, int tag, int type, Message message);

    Message waitMessage(int senderRank, int tag, int type);

    void sendWordsToRank(int receiverRank, int senderRank, int tag, int type,
                         const std::vector<int64_t> &words, const std::string &text);

    static void routeSend(int physicalTag, const std::vector<int64_t> &request);

    static std::vector<int64_t> routeReceive(int physicalTag);

    static TcpSoftwareSwitchTransport &routeTransport();

    int _rank = 0;
    std::unique_ptr<RoutedPeerTransport> _peerTransport;
    std::mutex _mutex;
    std::condition_variable _cv;
    std::map<MessageKey, std::deque<Message>> _pending;
    bool _finalized = false;
};

#endif
