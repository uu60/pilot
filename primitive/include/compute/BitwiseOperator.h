#ifndef PILOT_BITWISE_OPERATOR_H
#define PILOT_BITWISE_OPERATOR_H

#include "compute/SecureOperator.h"

class BitwiseOperator : public SecureOperator {
public:
    BitwiseOperator(std::vector<int64_t> &zs, int width, int messageTag, int clientRank);

    BitwiseOperator(std::vector<int64_t> *xs, std::vector<int64_t> *ys, int width, int messageTag,
                         int clientRank);

    BitwiseOperator *reconstruct(int clientRank) override;
    BitwiseOperator *execute() override;
};

#endif
