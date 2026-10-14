#ifndef PILOT_MESSAGE_INBOX_H
#define PILOT_MESSAGE_INBOX_H

#include "comm/protocol/PilotMessage.h"

#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>

class MessageInbox {
public:
    void push(PilotMessage message);

    PilotMessage wait(int senderRank, int tag, int type);

    void close();

private:
    struct Key {
        int senderRank{};
        int tag{};
        int type{};

        bool operator<(const Key &other) const;
    };

    std::mutex _mutex;
    std::condition_variable _cv;
    std::map<Key, std::deque<PilotMessage>> _pending;
    bool _closed = false;
};

#endif
