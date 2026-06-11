#ifndef PILOT_BITWISE_MUTEX_OPERATOR_H
#define PILOT_BITWISE_MUTEX_OPERATOR_H

#include "compute/BitwiseOperator.h"
#include "intermediate/item/BitwiseBmt.h"

#include <atomic>

class BitwiseMutexOperator : public BitwiseOperator {
public:
    std::vector<int64_t> *_conds_i{};
    inline static std::atomic_int64_t _totalTime = 0;

private:
    std::vector<BitwiseBmt> *_bmts{};
    bool _bidir{};

public:
    BitwiseMutexOperator(std::vector<int64_t> *xs, std::vector<int64_t> *ys, std::vector<int64_t> *conds,
                              int width, int clientRank);

    BitwiseMutexOperator(std::vector<int64_t> *xs, std::vector<int64_t> *ys, std::vector<int64_t> *conds,
                              int width);

    ~BitwiseMutexOperator() override;

    BitwiseMutexOperator *execute() override;
    BitwiseMutexOperator *setBmts(std::vector<BitwiseBmt> *bmts);

    static int tagStride();
    static int bmtCount(int num, int width);

private:
    bool prepareBmts(std::vector<BitwiseBmt> &bmts);
    void execute0();
    void executeBidirectionally();
};

#endif
