#include "comm/RoutedComm.h"

#include "comm/InPathSwitchSimulator.h"
#include "comm/item/FutureRequestWrapper.h"
#include "comm/transport/peer/TapRoutedPeerTransport.h"
#include "comm/transport/peer/TcpRoutedPeerTransport.h"
#include "comm/transport/switch/TcpSoftwareSwitchTransport.h"
#include "conf/Conf.h"
#include "intermediate/IntermediateDataSupport.h"

#include <future>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
int inPathPhysicalTag() {
    return Comm::TAG_SWITCH_LANE_BASE + IntermediateDataSupport::currentLane();
}

int serverPeerRank(int rank) {
    if (rank == Comm::SERVER0_RANK) {
        return Comm::SERVER1_RANK;
    }
    if (rank == Comm::SERVER1_RANK) {
        return Comm::SERVER0_RANK;
    }
    throw std::runtime_error("Current rank is not a routed server party.");
}

void validateRoutedNetworkMode() {
    if (Conf::ROUTED_NETWORK == Conf::ROUTED_NETWORK_TCP) {
        if (Conf::SIMULATION_LEVEL == Conf::SIMULATION_SIMULATOR) {
            throw std::runtime_error(
                "Simulator-level routed mode cannot use routed_network=tcp. "
                "TCP connects directly to the peer process, so an external L2 switch simulator cannot transparently "
                "append BMT. Use --routed_network=tap after the TAP backend and OS bridge are configured.");
        }
        return;
    }

    if (Conf::SIMULATION_LEVEL == Conf::SIMULATION_SOFTWARE) {
        throw std::runtime_error("routed_network=tap is only supported for simulator-level switch mode.");
    }
}

std::unique_ptr<RoutedPeerTransport> makePeerTransport() {
    if (Conf::ROUTED_NETWORK == Conf::ROUTED_NETWORK_TCP) {
        return std::make_unique<TcpRoutedPeerTransport>();
    }
    return std::make_unique<TapRoutedPeerTransport>();
}
}

bool RoutedComm::MessageKey::operator<(const MessageKey &other) const {
    if (senderRank != other.senderRank) {
        return senderRank < other.senderRank;
    }
    if (tag != other.tag) {
        return tag < other.tag;
    }
    return type < other.type;
}

bool RoutedComm::enabled() {
    return Conf::COMM_TYPE == Conf::ROUTED && Conf::ENABLE_IN_PATH_BMT_SWITCH;
}

void RoutedComm::runSwitch() {
    if (!enabled() || Comm::rank() != Conf::IN_PATH_SWITCH_RANK) {
        return;
    }
    if (Conf::SIMULATION_LEVEL == Conf::SIMULATION_SIMULATOR) {
        throw std::runtime_error(
            "Simulator-level switch mode expects an external in-path switch simulator, not a pilot switch role.");
    }
    TcpSoftwareSwitchTransport::runSwitch();
}

void RoutedComm::sendShutdown() {
    if (!Comm::isServerRank(Comm::rank())) {
        return;
    }
    if (Conf::SIMULATION_LEVEL == Conf::SIMULATION_SIMULATOR) {
        return;
    }
    std::vector<int64_t> stop{InPathSwitchSimulator::SHUTDOWN_MAGIC};
    routeTransport().send(PilotFrame{Comm::rank(), Conf::IN_PATH_SWITCH_RANK, InPathSwitchSimulator::CONTROL_TAG, stop});
}

int RoutedComm::rank_() {
    return _rank;
}

void RoutedComm::init_(int argc, char **argv) {
    (void) argc;
    (void) argv;
    validateRoutedNetworkMode();
    _rank = Conf::ROUTED_RANK;
    _peerTransport = makePeerTransport();
    _peerTransport->init(_rank, [this](RoutedPeerMessage message) {
        Message queued;
        queued.words = std::move(message.words);
        queued.text = std::move(message.text);
        enqueueMessage(message.senderRank, message.tag, message.type, std::move(queued));
    });
}

void RoutedComm::finalize_() {
    if (Comm::isServerRank(_rank) && Conf::SIMULATION_LEVEL != Conf::SIMULATION_SIMULATOR) {
        routeTransport().finalize();
    }
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_finalized) {
            return;
        }
        _finalized = true;
    }
    if (_peerTransport) {
        _peerTransport->finalize();
        _peerTransport.reset();
    }
}

bool RoutedComm::isServer_() {
    return _rank == SERVER0_RANK || _rank == SERVER1_RANK;
}

bool RoutedComm::isClient_() {
    return _rank == CLIENT_RANK;
}

void RoutedComm::send_(const std::vector<int64_t> &source, int width, int receiverRank, int tag) {
    (void) width;
    sendWordsToRank(receiverRank, _rank, tag, VECTOR_MESSAGE, source, {});
}

void RoutedComm::send_(int64_t source, int width, int receiverRank, int tag) {
    (void) width;
    sendWordsToRank(receiverRank, _rank, tag, INT64_MESSAGE, std::vector<int64_t>{source}, {});
}

void RoutedComm::send_(const std::string &source, int receiverRank, int tag) {
    sendWordsToRank(receiverRank, _rank, tag, STRING_MESSAGE, {}, source);
}

void RoutedComm::receive_(int64_t &source, int width, int senderRank, int tag) {
    (void) width;
    auto message = waitMessage(senderRank, tag, INT64_MESSAGE);
    if (message.words.empty()) {
        throw std::runtime_error("Missing routed scalar payload.");
    }
    source = message.words[0];
}

void RoutedComm::receive_(std::vector<int64_t> &source, int width, int senderRank, int tag) {
    (void) width;
    source = std::move(waitMessage(senderRank, tag, VECTOR_MESSAGE).words);
}

void RoutedComm::receive_(std::string &target, int senderRank, int tag) {
    target = std::move(waitMessage(senderRank, tag, STRING_MESSAGE).text);
}

AbstractRequest *RoutedComm::sendAsync_(const std::vector<int64_t> &source, int width, int receiverRank, int tag) {
    return new FutureRequestWrapper(std::async(std::launch::async, [=, this]() {
        send_(source, width, receiverRank, tag);
    }));
}

AbstractRequest *RoutedComm::sendAsync_(const int64_t &source, int width, int receiverRank, int tag) {
    return new FutureRequestWrapper(std::async(std::launch::async, [=, this]() {
        send_(source, width, receiverRank, tag);
    }));
}

AbstractRequest *RoutedComm::sendAsync_(const std::string &source, int receiverRank, int tag) {
    return new FutureRequestWrapper(std::async(std::launch::async, [=, this]() {
        send_(source, receiverRank, tag);
    }));
}

AbstractRequest *RoutedComm::receiveAsync_(int64_t &target, int width, int senderRank, int tag) {
    return new FutureRequestWrapper(std::async(std::launch::async, [&target, width, senderRank, tag, this]() {
        receive_(target, width, senderRank, tag);
    }));
}

AbstractRequest *RoutedComm::receiveAsync_(std::vector<int64_t> &target, int count, int width, int senderRank,
                                               int tag) {
    return new FutureRequestWrapper(std::async(std::launch::async, [&target, count, width, senderRank, tag, this]() {
        receive_(target, width, senderRank, tag);
        if (count >= 0 && static_cast<int>(target.size()) > count) {
            target.resize(count);
        }
    }));
}

AbstractRequest *RoutedComm::receiveAsync_(std::string &target, int length, int senderRank, int tag) {
    return new FutureRequestWrapper(std::async(std::launch::async, [&target, length, senderRank, tag, this]() {
        receive_(target, senderRank, tag);
        if (length >= 0 && static_cast<int>(target.size()) > length) {
            target.resize(length);
        }
    }));
}

void RoutedComm::serverSendImpl_(const int64_t &source, int width, int tag) {
    if (!Comm::isServerRank(_rank)) {
        Comm::serverSendImpl_(source, width, tag);
        return;
    }
    std::vector<int64_t> payload{source};
    auto request = InPathSwitchSimulator::makeSwitchRequest(
        tag, IntermediateDataSupport::consumeBitwiseBmtRequestCount(), payload);
    const int physicalTag = inPathPhysicalTag();
    if (Conf::SIMULATION_LEVEL == Conf::SIMULATION_SIMULATOR) {
        sendWordsToRank(::serverPeerRank(_rank), _rank, physicalTag, VECTOR_MESSAGE, request, {});
        return;
    }
    routeSend(physicalTag, request);
}

void RoutedComm::serverSendImpl_(const std::vector<int64_t> &source, int width, int tag) {
    if (!Comm::isServerRank(_rank)) {
        Comm::serverSendImpl_(source, width, tag);
        return;
    }
    auto request = InPathSwitchSimulator::makeSwitchRequest(
        tag, IntermediateDataSupport::consumeBitwiseBmtRequestCount(), source);
    const int physicalTag = inPathPhysicalTag();
    if (Conf::SIMULATION_LEVEL == Conf::SIMULATION_SIMULATOR) {
        sendWordsToRank(::serverPeerRank(_rank), _rank, physicalTag, VECTOR_MESSAGE, request, {});
        return;
    }
    routeSend(physicalTag, request);
}

void RoutedComm::serverReceiveImpl_(int64_t &source, int width, int tag) {
    if (!Comm::isServerRank(_rank)) {
        Comm::serverReceiveImpl_(source, width, tag);
        return;
    }
    const int physicalTag = inPathPhysicalTag();
    const auto envelope = Conf::SIMULATION_LEVEL == Conf::SIMULATION_SIMULATOR
                              ? waitMessage(::serverPeerRank(_rank), physicalTag, VECTOR_MESSAGE).words
                              : routeReceive(physicalTag);
    auto payload = InPathSwitchSimulator::unpackPayload(envelope);
    if (payload.empty()) {
        throw std::runtime_error("Missing scalar payload in routed switch envelope.");
    }
    source = payload[0];
}

void RoutedComm::serverReceiveImpl_(std::vector<int64_t> &source, int width, int tag) {
    if (!Comm::isServerRank(_rank)) {
        Comm::serverReceiveImpl_(source, width, tag);
        return;
    }
    const int physicalTag = inPathPhysicalTag();
    const auto envelope = Conf::SIMULATION_LEVEL == Conf::SIMULATION_SIMULATOR
                              ? waitMessage(::serverPeerRank(_rank), physicalTag, VECTOR_MESSAGE).words
                              : routeReceive(physicalTag);
    source = InPathSwitchSimulator::unpackPayload(envelope);
}

AbstractRequest *RoutedComm::serverSendAsyncImpl_(const int64_t &source, int width, int tag) {
    if (!Comm::isServerRank(_rank)) {
        return Comm::serverSendAsyncImpl_(source, width, tag);
    }
    const int physicalTag = inPathPhysicalTag();
    std::vector<int64_t> payload{source};
    auto request = InPathSwitchSimulator::makeSwitchRequest(
        tag, IntermediateDataSupport::consumeBitwiseBmtRequestCount(), payload);
    const int peerRank = ::serverPeerRank(_rank);
    return new FutureRequestWrapper(std::async(std::launch::async, [this, physicalTag, peerRank, request = std::move(request)]() {
        if (Conf::SIMULATION_LEVEL == Conf::SIMULATION_SIMULATOR) {
            sendWordsToRank(peerRank, Comm::rank(), physicalTag, VECTOR_MESSAGE, request, {});
            return;
        }
        routeSend(physicalTag, request);
    }));
}

AbstractRequest *RoutedComm::serverSendAsyncImpl_(const std::vector<int64_t> &source, int width, int tag) {
    if (!Comm::isServerRank(_rank)) {
        return Comm::serverSendAsyncImpl_(source, width, tag);
    }
    const int physicalTag = inPathPhysicalTag();
    auto request = InPathSwitchSimulator::makeSwitchRequest(
        tag, IntermediateDataSupport::consumeBitwiseBmtRequestCount(), source);
    const int peerRank = ::serverPeerRank(_rank);
    return new FutureRequestWrapper(std::async(std::launch::async, [this, physicalTag, peerRank, request = std::move(request)]() {
        if (Conf::SIMULATION_LEVEL == Conf::SIMULATION_SIMULATOR) {
            sendWordsToRank(peerRank, Comm::rank(), physicalTag, VECTOR_MESSAGE, request, {});
            return;
        }
        routeSend(physicalTag, request);
    }));
}

AbstractRequest *RoutedComm::serverReceiveAsyncImpl_(int64_t &target, int width, int tag) {
    if (!Comm::isServerRank(_rank)) {
        return Comm::serverReceiveAsyncImpl_(target, width, tag);
    }
    const int physicalTag = inPathPhysicalTag();
    const int peerRank = ::serverPeerRank(_rank);
    return new FutureRequestWrapper(std::async(std::launch::async, [&target, physicalTag, peerRank, this]() {
        const auto envelope = Conf::SIMULATION_LEVEL == Conf::SIMULATION_SIMULATOR
                                  ? waitMessage(peerRank, physicalTag, VECTOR_MESSAGE).words
                                  : routeReceive(physicalTag);
        auto payload = InPathSwitchSimulator::unpackPayload(envelope);
        if (payload.empty()) {
            throw std::runtime_error("Missing scalar payload in routed switch envelope.");
        }
        target = payload[0];
    }));
}

AbstractRequest *RoutedComm::serverReceiveAsyncImpl_(std::vector<int64_t> &target, int count, int width, int tag) {
    if (!Comm::isServerRank(_rank)) {
        return Comm::serverReceiveAsyncImpl_(target, count, width, tag);
    }
    const int physicalTag = inPathPhysicalTag();
    const int peerRank = ::serverPeerRank(_rank);
    return new FutureRequestWrapper(std::async(std::launch::async, [&target, count, physicalTag, peerRank, this]() {
        const auto envelope = Conf::SIMULATION_LEVEL == Conf::SIMULATION_SIMULATOR
                                  ? waitMessage(peerRank, physicalTag, VECTOR_MESSAGE).words
                                  : routeReceive(physicalTag);
        target = InPathSwitchSimulator::unpackPayload(envelope);
        if (count >= 0 && static_cast<int>(target.size()) > count) {
            target.resize(count);
        }
    }));
}

void RoutedComm::enqueueMessage(int senderRank, int tag, int type, Message message) {
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _pending[MessageKey{senderRank, tag, type}].push_back(std::move(message));
    }
    _cv.notify_all();
}

RoutedComm::Message RoutedComm::waitMessage(int senderRank, int tag, int type) {
    std::unique_lock<std::mutex> lock(_mutex);
    const MessageKey key{senderRank, tag, type};
    _cv.wait(lock, [&] { return !_pending[key].empty(); });
    auto message = std::move(_pending[key].front());
    _pending[key].pop_front();
    return message;
}

void RoutedComm::sendWordsToRank(int receiverRank, int senderRank, int tag, int type,
                                 const std::vector<int64_t> &words, const std::string &text) {
    if (!_peerTransport) {
        throw std::runtime_error("Routed peer transport is not initialized.");
    }
    _peerTransport->send(RoutedPeerMessage{senderRank, receiverRank, tag, type, words, text});
}

void RoutedComm::routeSend(int physicalTag, const std::vector<int64_t> &request) {
    routeTransport().send(PilotFrame{Comm::rank(), Conf::IN_PATH_SWITCH_RANK, physicalTag, request});
}

std::vector<int64_t> RoutedComm::routeReceive(int physicalTag) {
    return routeTransport().receive(physicalTag).words;
}

TcpSoftwareSwitchTransport &RoutedComm::routeTransport() {
    static TcpSoftwareSwitchTransport transport;
    return transport;
}
