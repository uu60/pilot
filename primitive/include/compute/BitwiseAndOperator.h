#ifndef PILOT_BITWISE_AND_OPERATOR_H
#define PILOT_BITWISE_AND_OPERATOR_H

#include "compute/BitwiseOperator.h"
#include "intermediate/item/BitwiseBmt.h"

#include <atomic>
#include <vector>

class BitwiseAndOperator : public BitwiseOperator {
private:
    std::vector<BitwiseBmt> *_bmts{};
    std::vector<int64_t> *_conds_i{};
    bool _doWithConditions{};

public:
    inline static std::atomic_int64_t _totalTime = 0;

    BitwiseAndOperator(std::vector<int64_t> *xs, std::vector<int64_t> *ys, int width,
                            int clientRank)
        : BitwiseOperator(xs, ys, width, 0, clientRank) {}

    BitwiseAndOperator(std::vector<int64_t> *xs, std::vector<int64_t> *ys, std::vector<int64_t> *conds,
                            int width);

    BitwiseAndOperator *execute() override;
    BitwiseAndOperator *setBmts(std::vector<BitwiseBmt> *bmts);

    static int tagStride();
    static int bmtCount(int num, int width);

private:
    int prepareBmts(std::vector<BitwiseBmt> &bmts);
    void execute0();
    void executeForMutex();
};

#endif
