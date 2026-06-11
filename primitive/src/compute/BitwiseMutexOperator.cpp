#include "compute/BitwiseMutexOperator.h"

#include "comm/Comm.h"
#include "compute/BitwiseAndOperator.h"

BitwiseMutexOperator::BitwiseMutexOperator(std::vector<int64_t> *xs, std::vector<int64_t> *ys,
                                                     std::vector<int64_t> *conds, int width,
                                                     int clientRank)
    : BitwiseOperator(xs, ys, width, 0, clientRank) {
    if (clientRank == NO_CLIENT_COMPUTE) {
        if (Comm::isClient()) return;
        _conds_i = new std::vector(*conds);
    } else {
        _conds_i = new std::vector(std::move(BitwiseOperator(*conds, 1, 0, clientRank)._zis));
    }
    if (Comm::isClient()) return;
    for (int64_t &ci: *_conds_i) {
        if (ci != 0) ci = ring(-1ll);
    }
}

BitwiseMutexOperator::BitwiseMutexOperator(std::vector<int64_t> *xs, std::vector<int64_t> *ys,
                                                     std::vector<int64_t> *conds, int width)
    : BitwiseOperator(xs, ys, width, 0, NO_CLIENT_COMPUTE) {
    _conds_i = new std::vector(*conds);
    for (int64_t &ci: *_conds_i) {
        if (ci != 0) ci = ring(-1ll);
    }
    _bidir = true;
}

BitwiseMutexOperator::~BitwiseMutexOperator() {
    delete _conds_i;
}

bool BitwiseMutexOperator::prepareBmts(std::vector<BitwiseBmt> &bmts) {
    if (_bmts != nullptr) {
        bmts = std::move(*_bmts);
        return true;
    }
    return false;
}

void BitwiseMutexOperator::execute0() {
    std::vector<BitwiseBmt> bmts;
    bool gotBmt = prepareBmts(bmts);
    const auto num = _conds_i->size();
    auto zis = BitwiseAndOperator(_xis, _yis, _conds_i, _width)
            .setBmts(gotBmt ? &bmts : nullptr)->execute()->_zis;
    _zis.resize(num);
    for (size_t i = 0; i < num; ++i) {
        _zis[i] = ring(zis[i] ^ (*_yis)[i] ^ zis[num + i]);
    }
}

void BitwiseMutexOperator::executeBidirectionally() {
    std::vector<BitwiseBmt> bmts;
    bool gotBmt = prepareBmts(bmts);
    const auto num = _xis->size();
    auto zis = BitwiseAndOperator(_xis, _yis, _conds_i, _width)
            .setBmts(gotBmt ? &bmts : nullptr)->execute()->_zis;
    _zis.resize(num * 2);
    for (size_t i = 0; i < num; ++i) {
        _zis[i] = ring(zis[i] ^ (*_yis)[i] ^ zis[num + i]);
        _zis[num + i] = ring(zis[i] ^ (*_xis)[i] ^ zis[num + i]);
    }
}

BitwiseMutexOperator *BitwiseMutexOperator::execute() {
    if (Comm::isClient()) return this;
    if (_bidir) executeBidirectionally(); else execute0();
    return this;
}

BitwiseMutexOperator *BitwiseMutexOperator::setBmts(std::vector<BitwiseBmt> *bmts) {
    _bmts = bmts;
    return this;
}

int BitwiseMutexOperator::tagStride() {
    return BitwiseAndOperator::tagStride();
}

int BitwiseMutexOperator::bmtCount(int num, int width) {
    return 2 * BitwiseAndOperator::bmtCount(num, width);
}
