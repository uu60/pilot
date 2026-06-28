#include "comm/transport/peer/TapRoutedPeerTransport.h"

#include "conf/Conf.h"

#include <stdexcept>

void TapRoutedPeerTransport::init(int rank, MessageHandler handler) {
    (void) rank;
    (void) handler;
    throw std::runtime_error(
        "routed_network=tap selected, but the TAP peer backend is not implemented yet. "
        "Expected future setup: server0 uses " + Conf::ROUTED_TAP_SERVER0 +
        ", server1 uses " + Conf::ROUTED_TAP_SERVER1 +
        ", and an external switch simulator bridges those TAP links.");
}

void TapRoutedPeerTransport::send(const RoutedPeerMessage &message) {
    (void) message;
    throw std::runtime_error("TAP routed peer transport is not initialized.");
}

void TapRoutedPeerTransport::finalize() {}
