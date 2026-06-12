#ifndef PILOT_BITWISE_LESS_OPERATOR_H
#define PILOT_BITWISE_LESS_OPERATOR_H

#include "compute/BitwiseOperator.h"

#include <atomic>

class BitwiseLessOperator : public BitwiseOperator {
public:
    inline static std::atomic_int64_t _totalTime = 0;

    BitwiseLessOperator(std::vector<int64_t> *xs, std::vector<int64_t> *ys, int width,
                             int clientRank)
        : BitwiseOperator(ys, xs, width, 0, clientRank) {}

    BitwiseLessOperator *execute() override;

private:
    std::vector<int64_t> shiftGreater(std::vector<int64_t> &in, int r) const;
};

#endif
