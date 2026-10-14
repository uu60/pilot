#ifndef PILOT_MESSAGE_H
#define PILOT_MESSAGE_H

#include <cstdint>
#include <string>
#include <vector>

struct PilotMessage {
    int senderRank{};
    int receiverRank{};
    int tag{};
    int type{};
    std::vector<int64_t> words;
    std::string text;
};

#endif
