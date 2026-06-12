#include "compute/BitwiseAndOperator.h"

#include "comm/Comm.h"
#include "conf/Conf.h"
#include "intermediate/IntermediateDataSupport.h"
#include "intermediate/item/BitwiseBmt.h"
#include "utils/Math.h"

#include <algorithm>
#include <stdexcept>

void BitwiseAndOperator::prepareBmts(std::vector<BitwiseBmt> &bmts) {
    const auto actualBmtCount = static_cast<int>(_xis->size() * (_doWithConditions ? 2 : 1));
    if (actualBmtCount > Conf::IN_PATH_BMT_BUNDLE_SIZE) {
        throw std::runtime_error("BitwiseAnd batch exceeds fixed in-path BMT bundle size.");
    }
    bmts = IntermediateDataSupport::pollBitwiseBmts(Conf::IN_PATH_BMT_BUNDLE_SIZE, 64);
}

BitwiseAndOperator::BitwiseAndOperator(std::vector<int64_t> *xs, std::vector<int64_t> *ys,
                                                 std::vector<int64_t> *conds, int width)
    : BitwiseOperator(xs, ys, width, 0, NO_CLIENT_COMPUTE) {
    _conds_i = conds;
    _doWithConditions = true;
}

BitwiseAndOperator *BitwiseAndOperator::execute() {
    if (Comm::isClient()) return this;

    if (_doWithConditions) {
        executeForMutex();
    } else {
        execute0();
    }
    return this;
}

void BitwiseAndOperator::execute0() {
    std::vector<BitwiseBmt> bmts;
    prepareBmts(bmts);
    const int num = static_cast<int>(_xis->size());
    const int paddedNum = std::max(num, Conf::IN_PATH_BMT_BUNDLE_SIZE);

    std::vector<int64_t> efi(paddedNum * 2, 0);
    if (_width < 64) {
        for (int i = 0; i < num; i++) {
            auto bmt = BitwiseBmt::extract(bmts, i, _width);
            efi[i] = (*_xis)[i] ^ bmt._a;
            efi[paddedNum + i] = (*_yis)[i] ^ bmt._b;
        }
    } else {
        for (int i = 0; i < num; i++) {
            efi[i] = (*_xis)[i] ^ bmts[i]._a;
            efi[paddedNum + i] = (*_yis)[i] ^ bmts[i]._b;
        }
    }

    std::vector<int64_t> efo;
    auto r0 = Comm::serverSendAsync(efi, _width);
    auto r1 = Comm::serverReceiveAsync(efo, static_cast<int>(efi.size()), _width);
    Comm::wait(r0);
    Comm::wait(r1);

    std::vector<int64_t> efs(efi.size());
    for (size_t i = 0; i < efi.size(); ++i) efs[i] = efi[i] ^ efo[i];

    _zis.resize(num);
    const int64_t extendedRank = Comm::rank() ? ring(-1ll) : 0;
    if (_width < 64) {
        for (int i = 0; i < num; i++) {
            const int64_t e = efs[i];
            const int64_t f = efs[paddedNum + i];
            auto bmt = BitwiseBmt::extract(bmts, i, _width);
            _zis[i] = Math::ring((extendedRank & e & f) ^ (f & bmt._a) ^ (e & bmt._b) ^ bmt._c, _width);
        }
    } else {
        for (int i = 0; i < num; i++) {
            const int64_t e = efs[i];
            const int64_t f = efs[paddedNum + i];
            _zis[i] = Math::ring((extendedRank & e & f) ^ (f & bmts[i]._a) ^ (e & bmts[i]._b) ^ bmts[i]._c,
                                 _width);
        }
    }
}

void BitwiseAndOperator::executeForMutex() {
    std::vector<BitwiseBmt> bmts;
    prepareBmts(bmts);
    const int num = static_cast<int>(_xis->size());
    const int condNum = static_cast<int>(_conds_i->size());
    const int paddedNum = std::max(num, (Conf::IN_PATH_BMT_BUNDLE_SIZE + 1) / 2);

    std::vector<int64_t> efi(paddedNum * 4, 0);
    if (_width < 64) {
        for (int i = 0; i < num; i++) {
            auto bmt = BitwiseBmt::extract(bmts, i, _width);
            efi[i] = (*_xis)[i] ^ bmt._a;
            efi[paddedNum * 2 + i] = (*_conds_i)[i % condNum] ^ bmt._b;
        }
        for (int i = num; i < num * 2; i++) {
            auto bmt = BitwiseBmt::extract(bmts, i, _width);
            efi[paddedNum + i - num] = (*_yis)[i - num] ^ bmt._a;
            efi[paddedNum * 3 + i - num] = (*_conds_i)[i % condNum] ^ bmt._b;
        }
    } else {
        for (int i = 0; i < num; i++) {
            efi[i] = (*_xis)[i] ^ bmts[i]._a;
            efi[paddedNum * 2 + i] = (*_conds_i)[i % condNum] ^ bmts[i]._b;
        }
        for (int i = num; i < num * 2; i++) {
            efi[paddedNum + i - num] = (*_yis)[i - num] ^ bmts[i]._a;
            efi[paddedNum * 3 + i - num] = (*_conds_i)[i % condNum] ^ bmts[i]._b;
        }
    }

    std::vector<int64_t> efo;
    auto r0 = Comm::serverSendAsync(efi, _width);
    auto r1 = Comm::serverReceiveAsync(efo, static_cast<int>(efi.size()), _width);
    Comm::wait(r0);
    Comm::wait(r1);

    std::vector<int64_t> efs(efi.size());
    for (size_t i = 0; i < efi.size(); ++i) efs[i] = efi[i] ^ efo[i];

    _zis.resize(num * 2);
    const int64_t extendedRank = Comm::rank() ? ring(-1ll) : 0;
    for (int i = 0; i < num * 2; i++) {
        const int eIdx = i < num ? i : paddedNum + i - num;
        const int fIdx = i < num ? paddedNum * 2 + i : paddedNum * 3 + i - num;
        const int64_t e = efs[eIdx];
        const int64_t f = efs[fIdx];
        if (_width < 64) {
            auto bmt = BitwiseBmt::extract(bmts, i, _width);
            _zis[i] = Math::ring((extendedRank & e & f) ^ (f & bmt._a) ^ (e & bmt._b) ^ bmt._c, _width);
        } else {
            _zis[i] = Math::ring((extendedRank & e & f) ^ (f & bmts[i]._a) ^ (e & bmts[i]._b) ^ bmts[i]._c,
                                 _width);
        }
    }
}
