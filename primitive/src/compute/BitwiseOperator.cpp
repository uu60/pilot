#include "compute/BitwiseOperator.h"

#include "comm/Comm.h"
#include "utils/Math.h"

#include <stdexcept>

BitwiseOperator::BitwiseOperator(std::vector<int64_t> &zs, int width, int ioTag, int clientRank)
    : SecureOperator(width, ioTag) {
    if (clientRank < 0) {
        _zis = zs;
    } else if (Comm::rank() == clientRank) {
        std::vector<int64_t> zv0, zv1;
        zv0.reserve(zs.size());
        zv1.reserve(zs.size());
        for (auto z: zs) {
            int64_t z1 = ring(Math::randInt());
            int64_t z0 = ring(z ^ z1);
            zv0.push_back(z0);
            zv1.push_back(z1);
        }
        auto r0 = Comm::sendAsync(zv0, _width, Comm::SERVER0_RANK, buildIoTag());
        auto r1 = Comm::sendAsync(zv1, _width, Comm::SERVER1_RANK, buildIoTag());
        Comm::wait(r0);
        Comm::wait(r1);
    } else if (Comm::isServer()) {
        Comm::receive(_zis, _width, clientRank, buildIoTag());
    }
}

BitwiseOperator::BitwiseOperator(std::vector<int64_t> *xs, std::vector<int64_t> *ys, int width,
                                           int ioTag, int clientRank)
    : SecureOperator(width, ioTag) {
    if (clientRank < 0) {
        _xis = xs;
        _yis = ys;
    } else if (Comm::rank() == clientRank) {
        std::vector<int64_t> v0, v1;
        const size_t size = xs->size();
        v0.reserve(2 * size);
        v1.reserve(2 * size);
        for (auto x: *xs) {
            int64_t x1 = ring(Math::randInt());
            int64_t x0 = ring(x ^ x1);
            v0.push_back(x0);
            v1.push_back(x1);
        }
        for (auto y: *ys) {
            int64_t y1 = ring(Math::randInt());
            int64_t y0 = ring(y ^ y1);
            v0.push_back(y0);
            v1.push_back(y1);
        }
        auto r0 = Comm::sendAsync(v0, _width, Comm::SERVER0_RANK, buildIoTag());
        auto r1 = Comm::sendAsync(v1, _width, Comm::SERVER1_RANK, buildIoTag());
        Comm::wait(r0);
        Comm::wait(r1);
    } else if (Comm::isServer()) {
        std::vector<int64_t> temp;
        Comm::receive(temp, _width, clientRank, buildIoTag());
        const size_t size = temp.size() / 2;
        _xis = new std::vector<int64_t>(size);
        _yis = new std::vector<int64_t>(size);
        _dx = true;
        _dy = true;
        for (size_t i = 0; i < size; ++i) {
            (*_xis)[i] = temp[i];
            (*_yis)[i] = temp[i + size];
        }
    }
}

BitwiseOperator *BitwiseOperator::reconstruct(int clientRank) {
    if (Comm::isServer()) {
        Comm::send(_zis, _width, clientRank, buildIoTag());
    } else if (Comm::rank() == clientRank) {
        std::vector<int64_t> temp0, temp1;
        Comm::receive(temp0, _width, Comm::SERVER0_RANK, buildIoTag());
        Comm::receive(temp1, _width, Comm::SERVER1_RANK, buildIoTag());
        _results.clear();
        _results.reserve(temp0.size());
        for (size_t i = 0; i < temp0.size(); ++i) {
            _results.push_back(ring(temp0[i] ^ temp1[i]));
        }
    }
    return this;
}

BitwiseOperator *BitwiseOperator::execute() {
    throw std::runtime_error("Needs implementation.");
}
