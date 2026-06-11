#include "comm/item/FutureRequestWrapper.h"

FutureRequestWrapper::FutureRequestWrapper(std::future<void> future) : _future(std::move(future)) {}

void FutureRequestWrapper::wait() {
    _future.get();
}
