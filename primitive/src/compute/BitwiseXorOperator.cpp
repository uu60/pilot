#include "compute/BitwiseXorOperator.h"

#include "comm/Comm.h"

BitwiseXorOperator *BitwiseXorOperator::execute() {
    if (Comm::isClient()) return this;
    _zis.resize(_xis->size());
    for (size_t i = 0; i < _xis->size(); ++i) {
        _zis[i] = ring((*_xis)[i] ^ (*_yis)[i]);
    }
    return this;
}
