#ifndef PILOT_SOFTWARE_BMT_ENGINE_H
#define PILOT_SOFTWARE_BMT_ENGINE_H

#include <cstdint>
#include <vector>

class SoftwareBmtEngine {
public:
    static std::vector<int64_t> processRequest(int srcRank, int tag,
                                               const std::vector<int64_t> &request);
};

#endif
