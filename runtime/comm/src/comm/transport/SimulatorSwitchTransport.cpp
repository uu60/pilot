#include "comm/transport/SimulatorSwitchTransport.h"

#include <stdexcept>

namespace {
[[noreturn]] void throwNotImplemented() {
    throw std::runtime_error(
        "Simulator switch transport is intentionally not implemented in the MPI software-switch path.");
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
