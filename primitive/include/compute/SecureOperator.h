#ifndef PILOT_SECUREOPERATOR_H
#define PILOT_SECUREOPERATOR_H

#include <cstdint>
#include <vector>

#include "utils/Math.h"

class SecureOperator {
public:
    static constexpr int NO_CLIENT_COMPUTE = -1;

    int _width{};
    std::vector<int64_t> _results{};
    std::vector<int64_t> *_xis{};
    std::vector<int64_t> *_yis{};
    std::vector<int64_t> _zis{};
    bool _dx{};
    bool _dy{};

protected:
    int _ioTag{};

public:
    explicit SecureOperator(int width, int ioTag = 0)
        : _width(width), _ioTag(ioTag) {}

    virtual ~SecureOperator() {
        if (_dx) delete _xis;
        if (_dy) delete _yis;
    }

    virtual SecureOperator *execute() = 0;
    virtual SecureOperator *reconstruct(int clientRank) = 0;

protected:
    [[nodiscard]] int64_t ring(int64_t raw) const {
        return Math::ring(raw, _width);
    }

    [[nodiscard]] int buildIoTag() const {
        return _ioTag;
    }
};

#endif
