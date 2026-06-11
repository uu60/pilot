#ifndef PILOT_SECUREOPERATOR_H
#define PILOT_SECUREOPERATOR_H
#include <cstdint>

class SecureOperator {
public:
    static constexpr int NO_CLIENT_COMPUTE = -1;
    int _width{};

protected:
    int _startMsgTag{};
    int _currentMsgTag{};

public:

    virtual ~SecureOperator() = default;

    explicit SecureOperator(int width, int msgTagOffset) : _width(width),
        _startMsgTag(msgTagOffset), _currentMsgTag(msgTagOffset) {
    }

    virtual SecureOperator *execute() = 0;

    virtual SecureOperator *reconstruct(int clientRank) = 0;

protected:
    [[nodiscard]] int64_t ring(int64_t raw) const {

    }
};


#endif //PILOT_SECUREOPERATOR_H
