#include "comm/TcpComm.h"

#include "comm/Comm.h"
#include "comm/InPathSwitchSimulator.h"
#include "comm/transport/TcpSoftwareSwitchTransport.h"
#include "comm/item/FutureRequestWrapper.h"
#include "conf/Conf.h"
#include "intermediate/IntermediateDataSupport.h"

#include <future>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
int inPathPhysicalTag() {
    return Comm::TAG_SWITCH_LANE_BASE + IntermediateDataSupport::currentLane();
}
}

bool TcpComm::enabled() {
    return Conf::ENABLE_IN_PATH_BMT_SWITCH && Conf::SERVER_TRANSPORT == Conf::SERVER_TRANSPORT_TCP;
}

void TcpComm::runSwitch() {
    if (!enabled() || Comm::rank() != Conf::IN_PATH_SWITCH_RANK) {
        return;
    }
    if (Conf::SIMULATION_LEVEL == Conf::SIMULATION_SIMULATOR) {
        throw std::runtime_error("TCP simulator-level switch is not connected yet.");
    }
    TcpSoftwareSwitchTransport::runSwitch();
}

void TcpComm::sendShutdown() {
    if (!enabled() || !Comm::isServerRank(Comm::rank())) {
        return;
    }
    std::vector<int64_t> stop{InPathSwitchSimulator::SHUTDOWN_MAGIC};
    transport().send(PilotFrame{Comm::rank(), Conf::IN_PATH_SWITCH_RANK, InPathSwitchSimulator::CONTROL_TAG, stop});
}

void TcpComm::serverSendImpl_(const int64_t &source, int width, int tag) {
    if (!Comm::isServerRank(Comm::rank())) {
        Comm::serverSendImpl_(source, width, tag);
        return;
    }

    const int physicalTag = inPathPhysicalTag();
    std::vector<int64_t> payload{source};
    auto request = InPathSwitchSimulator::makeSwitchRequest(
        tag, IntermediateDataSupport::consumeBitwiseBmtRequestCount(), payload);
    send(physicalTag, request);
}

void TcpComm::serverSendImpl_(const std::vector<int64_t> &source, int width, int tag) {
    if (!Comm::isServerRank(Comm::rank())) {
        Comm::serverSendImpl_(source, width, tag);
        return;
    }

    const int physicalTag = inPathPhysicalTag();
    auto request = InPathSwitchSimulator::makeSwitchRequest(
        tag, IntermediateDataSupport::consumeBitwiseBmtRequestCount(), source);
    send(physicalTag, request);
}

void TcpComm::serverReceiveImpl_(int64_t &source, int width, int tag) {
    if (!Comm::isServerRank(Comm::rank())) {
        Comm::serverReceiveImpl_(source, width, tag);
        return;
    }

    const int physicalTag = inPathPhysicalTag();
    auto payload = InPathSwitchSimulator::unpackPayload(receive(physicalTag));
    if (payload.empty()) {
        throw std::runtime_error("Missing scalar payload in in-path switch envelope.");
    }
    source = payload[0];
}

void TcpComm::serverReceiveImpl_(std::vector<int64_t> &source, int width, int tag) {
    if (!Comm::isServerRank(Comm::rank())) {
        Comm::serverReceiveImpl_(source, width, tag);
        return;
    }

    const int physicalTag = inPathPhysicalTag();
    source = InPathSwitchSimulator::unpackPayload(receive(physicalTag));
}

AbstractRequest *TcpComm::serverSendAsyncImpl_(const int64_t &source, int width, int tag) {
    if (!Comm::isServerRank(Comm::rank())) {
        return Comm::serverSendAsyncImpl_(source, width, tag);
    }

    const int physicalTag = inPathPhysicalTag();
    std::vector<int64_t> payload{source};
    auto request = InPathSwitchSimulator::makeSwitchRequest(
        tag, IntermediateDataSupport::consumeBitwiseBmtRequestCount(), payload);
    return new FutureRequestWrapper(std::async(std::launch::async, [physicalTag, request = std::move(request)]() {
        send(physicalTag, request);
    }));
}

AbstractRequest *TcpComm::serverSendAsyncImpl_(const std::vector<int64_t> &source, int width, int tag) {
    if (!Comm::isServerRank(Comm::rank())) {
        return Comm::serverSendAsyncImpl_(source, width, tag);
    }

    const int physicalTag = inPathPhysicalTag();
    auto request = InPathSwitchSimulator::makeSwitchRequest(
        tag, IntermediateDataSupport::consumeBitwiseBmtRequestCount(), source);
    return new FutureRequestWrapper(std::async(std::launch::async, [physicalTag, request = std::move(request)]() {
        send(physicalTag, request);
    }));
}

AbstractRequest *TcpComm::serverReceiveAsyncImpl_(int64_t &target, int width, int tag) {
    if (!Comm::isServerRank(Comm::rank())) {
        return Comm::serverReceiveAsyncImpl_(target, width, tag);
    }

    const int physicalTag = inPathPhysicalTag();
    return new FutureRequestWrapper(std::async(std::launch::async, [&target, physicalTag]() {
        auto payload = InPathSwitchSimulator::unpackPayload(receive(physicalTag));
        if (payload.empty()) {
            throw std::runtime_error("Missing scalar payload in in-path switch envelope.");
        }
        target = payload[0];
    }));
}

AbstractRequest *TcpComm::serverReceiveAsyncImpl_(std::vector<int64_t> &target, int count, int width, int tag) {
    if (!Comm::isServerRank(Comm::rank())) {
        return Comm::serverReceiveAsyncImpl_(target, count, width, tag);
    }

    const int physicalTag = inPathPhysicalTag();
    return new FutureRequestWrapper(std::async(std::launch::async, [&target, count, physicalTag]() {
        auto payload = InPathSwitchSimulator::unpackPayload(receive(physicalTag));
        if (count >= 0 && static_cast<int>(payload.size()) > count) {
            payload.resize(count);
        }
        target = std::move(payload);
    }));
}

void TcpComm::finalize_() {
    if (Conf::SIMULATION_LEVEL == Conf::SIMULATION_SOFTWARE) {
        transport().finalize();
    }
    MpiComm::finalize_();
}

SwitchFrameTransport &TcpComm::transport() {
    if (Conf::SIMULATION_LEVEL == Conf::SIMULATION_SIMULATOR) {
        throw std::runtime_error("TCP simulator-level switch transport is not connected yet.");
    }
    static TcpSoftwareSwitchTransport tcpTransport;
    return tcpTransport;
}

void TcpComm::send(int physicalTag, const std::vector<int64_t> &request) {
    transport().send(PilotFrame{Comm::rank(), Conf::IN_PATH_SWITCH_RANK, physicalTag, request});
}

std::vector<int64_t> TcpComm::receive(int physicalTag) {
    return transport().receive(physicalTag).words;
}
