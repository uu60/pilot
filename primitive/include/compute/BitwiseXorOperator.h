#ifndef PILOT_BITWISE_XOR_OPERATOR_H
#define PILOT_BITWISE_XOR_OPERATOR_H

#include "compute/BitwiseOperator.h"

class BitwiseXorOperator : public BitwiseOperator {
public:
    BitwiseXorOperator(std::vector<int64_t> *xs, std::vector<int64_t> *ys, int width,
                            int clientRank)
        : BitwiseOperator(xs, ys, width, 0, clientRank) {}

    BitwiseXorOperator *execute() override;
};

#endif
