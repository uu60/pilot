#include "compute/BitwiseEqualOperator.h"

#include "comm/Comm.h"
#include "compute/BitwiseAndOperator.h"
#include "compute/BitwiseXorOperator.h"
#include "utils/Math.h"

BitwiseEqualOperator *BitwiseEqualOperator::execute() {
    if (Comm::isClient()) return this;

    auto diff = BitwiseXorOperator(_xis, _yis, _width, NO_CLIENT_COMPUTE)
            .execute()
            ->_zis;

    std::vector<int64_t> values;
    values.reserve(diff.size() * _width);
    for (auto v: diff) {
        auto equalBits = Math::ring(Comm::rank() == Comm::SERVER0_RANK ? ~v : v, _width);
        for (int bit = 0; bit < _width; ++bit) {
            values.push_back((equalBits >> bit) & 1);
        }
    }

    int activeWidth = _width;

    while (activeWidth > 1) {
        const int pairCountPerRecord = activeWidth / 2;
        const int nextWidth = (activeWidth + 1) / 2;
        const int recordCount = static_cast<int>(diff.size());

        std::vector<int64_t> lhs;
        std::vector<int64_t> rhs;
        lhs.reserve(recordCount * pairCountPerRecord);
        rhs.reserve(recordCount * pairCountPerRecord);

        std::vector<int64_t> carry(recordCount, 0);
        for (int row = 0; row < recordCount; ++row) {
            auto base = static_cast<size_t>(row * activeWidth);
            for (int bit = 0; bit + 1 < activeWidth; bit += 2) {
                lhs.push_back(values[base + bit]);
                rhs.push_back(values[base + bit + 1]);
            }
            if (activeWidth % 2 == 1) {
                carry[row] = values[base + activeWidth - 1];
            }
        }

        std::vector<int64_t> andResult;
        if (!lhs.empty()) {
            andResult = BitwiseAndOperator(&lhs, &rhs, 1, NO_CLIENT_COMPUTE)
                    .execute()
                    ->_zis;
        }

        std::vector<int64_t> next;
        next.reserve(recordCount * nextWidth);
        size_t andIndex = 0;
        for (int row = 0; row < recordCount; ++row) {
            for (int j = 0; j < pairCountPerRecord; ++j) {
                next.push_back(andResult[andIndex++] & 1);
            }
            if (activeWidth % 2 == 1) {
                next.push_back(carry[row] & 1);
            }
        }

        values = std::move(next);
        activeWidth = nextWidth;
    }

    _zis = std::move(values);
    return this;
}
