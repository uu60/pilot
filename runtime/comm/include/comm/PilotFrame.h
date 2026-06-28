#ifndef PILOT_FRAME_H
#define PILOT_FRAME_H

#include <cstdint>
#include <vector>

struct PilotFrame {
    int sourceRank{};
    int destinationRank{};
    int physicalTag{};
    std::vector<int64_t> words;
};

#endif
