#ifndef PILOT_IN_PATH_TIMING_MODEL_H
#define PILOT_IN_PATH_TIMING_MODEL_H

#include <cstddef>

class InPathTimingModel {
public:
    static void recordRequest(int laneId,
                              int tag,
                              int destinationRank,
                              int bmtCount,
                              size_t payloadWords,
                              size_t encryptedTrailerBytes);
};

#endif
