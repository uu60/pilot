#include "comm/message/MessageInbox.h"

#include <stdexcept>
#include <utility>

bool MessageInbox::Key::operator<(const Key &other) const {
    if (senderRank != other.senderRank) {
        return senderRank < other.senderRank;
    }
    if (tag != other.tag) {
        return tag < other.tag;
    }
    return type < other.type;
}

void MessageInbox::push(PilotMessage message) {
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_closed) {
            return;
        }
        const Key key{message.senderRank, message.tag, message.type};
        _pending[key].push_back(std::move(message));
    }
    _cv.notify_all();
}

PilotMessage MessageInbox::wait(int senderRank, int tag, int type) {
    std::unique_lock<std::mutex> lock(_mutex);
    const Key key{senderRank, tag, type};
    _cv.wait(lock, [&] { return _closed || !_pending[key].empty(); });
    if (_pending[key].empty()) {
        throw std::runtime_error("Communication endpoint closed while waiting for a message.");
    }
    auto message = std::move(_pending[key].front());
    _pending[key].pop_front();
    return message;
}

void MessageInbox::close() {
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _closed = true;
    }
    _cv.notify_all();
}
