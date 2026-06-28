#include "comm/transport/SimulatorSwitchTransport.h"

#include <stdexcept>

namespace {
[[noreturn]] void throwNotImplemented() {
    throw std::runtime_error("Simulator switch transport is not implemented yet.");
}
}

void SimulatorSwitchTransport::send(const PilotFrame &frame) {
    (void) frame;
    throwNotImplemented();
}

PilotFrame SimulatorSwitchTransport::receive(int physicalTag) {
    (void) physicalTag;
    throwNotImplemented();
}

void SimulatorSwitchTransport::finalize() {
}

void SimulatorSwitchTransport::runSwitch() {
    throwNotImplemented();
}
