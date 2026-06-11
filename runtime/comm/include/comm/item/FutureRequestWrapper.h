#ifndef PILOT_FUTURE_REQUEST_WRAPPER_H
#define PILOT_FUTURE_REQUEST_WRAPPER_H

#include "AbstractRequest.h"

#include <future>

class FutureRequestWrapper : public AbstractRequest {
private:
    std::future<void> _future;

public:
    explicit FutureRequestWrapper(std::future<void> future);

    void wait() override;
};

#endif
