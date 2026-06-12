#include "compute/BitwiseLessOperator.h"

#include "comm/Comm.h"
#include "compute/BitwiseAndOperator.h"
#include "utils/Math.h"

#include <cmath>
#include <stdexcept>

BitwiseLessOperator *BitwiseLessOperator::execute() {
    if (Comm::isClient()) return this;

    std::vector<int64_t> x_xor_y(_xis->size());
    for (size_t i = 0; i < _xis->size(); ++i) x_xor_y[i] = (*_xis)[i] ^ (*_yis)[i];

    std::vector<int64_t> lbs;
    const int64_t mask = Math::ring(-1ll, _width);
    if (Comm::rank() == Comm::SERVER0_RANK) {
        lbs = x_xor_y;
    } else {
        lbs.reserve(x_xor_y.size());
        for (int64_t e: x_xor_y) lbs.push_back(e ^ mask);
    }

    auto shifted_1 = shiftGreater(lbs, 1);
    lbs = BitwiseAndOperator(&lbs, &shifted_1, _width, NO_CLIENT_COMPUTE).execute()->_zis;

    std::vector<int64_t> diag(x_xor_y.size());
    for (size_t i = 0; i < x_xor_y.size(); ++i) {
        diag[i] = Math::changeBit(x_xor_y[i], 0, Math::getBit((*_yis)[i], 0) ^ Comm::rank());
    }
    diag = BitwiseAndOperator(&diag, _xis, _width, NO_CLIENT_COMPUTE).execute()->_zis;

    const int rounds = static_cast<int>(std::floor(std::log2(_width)));
    for (int r = 2; r <= rounds; ++r) {
        auto shifted_r = shiftGreater(lbs, r);
        lbs = BitwiseAndOperator(&lbs, &shifted_r, _width, NO_CLIENT_COMPUTE).execute()->_zis;
    }

    std::vector<int64_t> shifted_accum;
    shifted_accum.reserve(lbs.size());
    for (size_t i = 0; i < lbs.size(); ++i) {
        shifted_accum.push_back(Math::changeBit(lbs[i] >> 1, _width - 1, Comm::rank()));
    }

    auto final_accum = BitwiseAndOperator(&shifted_accum, &diag, _width, NO_CLIENT_COMPUTE)
            .execute()->_zis;

    _zis.resize(final_accum.size());
    for (size_t i = 0; i < final_accum.size(); ++i) {
        bool result = false;
        for (int j = 0; j < _width; ++j) result ^= Math::getBit(final_accum[i], j);
        _zis[i] = result;
    }
    return this;
}

std::vector<int64_t> BitwiseLessOperator::shiftGreater(std::vector<int64_t> &in, int r) const {
    int part_size = 1 << r;
    if (part_size > _width) return in;
    int offset = part_size >> 1;
    std::vector<int64_t> out;
    out.reserve(in.size());
    for (int64_t ini: in) {
        for (int i = 0; i < _width; i += part_size) {
            int start = i + offset;
            if (start >= _width) break;
            bool midBit = Math::getBit(ini, start);
            int count = start - i;
            int64_t mask = ((1LL << count) - 1) << i;
            ini = midBit ? (ini | mask) : (ini & ~mask);
        }
        out.push_back(ini);
    }
    return out;
}
