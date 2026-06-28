#include "comm/RoutedComm.h"

#include "comm/Comm.h"
#include "comm/InPathSwitchSimulator.h"
#include "comm/transport/MpiSoftwareSwitchTransport.h"
#include "comm/transport/SimulatorSwitchTransport.h"
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

bool RoutedComm::enabled() {
    return Conf::ENABLE_IN_PATH_BMT_SWITCH;
}

void RoutedComm::runSwitch() {
    if (!enabled() || Comm::rank() != Conf::IN_PATH_SWITCH_RANK) {
        return;
    }
    if (Conf::SIMULATION_LEVEL == Conf::SIMULATION_SIMULATOR) {
        SimulatorSwitchTransport::runSwitch();
        return;
    }
    if (Conf::SERVER_TRANSPORT == Conf::SERVER_TRANSPORT_TCP) {
        TcpSoftwareSwitchTransport::runSwitch();
        return;
    }
    MpiSoftwareSwitchTransport::runSwitch();
}

void RoutedComm::sendShutdown() {
    if (!enabled() || !Comm::isServerRank(Comm::rank())) {
        return;
    }
    std::vector<int64_t> stop{InPathSwitchSimulator::SHUTDOWN_MAGIC};
    transport().send(PilotFrame{Comm::rank(), Conf::IN_PATH_SWITCH_RANK, InPathSwitchSimulator::CONTROL_TAG, stop});
}

void RoutedComm::serverSendImpl_(const int64_t &source, int width, int tag) {
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

void RoutedComm::serverSendImpl_(const std::vector<int64_t> &source, int width, int tag) {
    if (!Comm::isServerRank(Comm::rank())) {
        Comm::serverSendImpl_(source, width, tag);
        return;
    }

    const int physicalTag = inPathPhysicalTag();
    auto request = InPathSwitchSimulator::makeSwitchRequest(
        tag, IntermediateDataSupport::consumeBitwiseBmtRequestCount(), source);
    send(physicalTag, request);
}

void RoutedComm::serverReceiveImpl_(int64_t &source, int width, int tag) {
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

void RoutedComm::serverReceiveImpl_(std::vector<int64_t> &source, int width, int tag) {
    if (!Comm::isServerRank(Comm::rank())) {
        Comm::serverReceiveImpl_(source, width, tag);
        return;
    }

    const int physicalTag = inPathPhysicalTag();
    source = InPathSwitchSimulator::unpackPayload(receive(physicalTag));
}

AbstractRequest *RoutedComm::serverSendAsyncImpl_(const int64_t &source, int width, int tag) {
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

AbstractRequest *RoutedComm::serverSendAsyncImpl_(const std::vector<int64_t> &source, int width, int tag) {
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

AbstractRequest *RoutedComm::serverReceiveAsyncImpl_(int64_t &target, int width, int tag) {
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

AbstractRequest *RoutedComm::serverReceiveAsyncImpl_(std::vector<int64_t> &target, int count, int width, int tag) {
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

void RoutedComm::finalize_() {
    if (Conf::SIMULATION_LEVEL == Conf::SIMULATION_SOFTWARE ||
        Conf::SIMULATION_LEVEL == Conf::SIMULATION_SIMULATOR) {
        transport().finalize();
    }
    MpiComm::finalize_();
}

SwitchFrameTransport &RoutedComm::transport() {
    if (Conf::SIMULATION_LEVEL == Conf::SIMULATION_SIMULATOR) {
        static SimulatorSwitchTransport simulatorTransport;
        return simulatorTransport;
    }
    if (Conf::SERVER_TRANSPORT == Conf::SERVER_TRANSPORT_MPI) {
        static MpiSoftwareSwitchTransport mpiTransport;
        return mpiTransport;
    }
    static TcpSoftwareSwitchTransport tcpTransport;
    return tcpTransport;
}

void RoutedComm::send(int physicalTag, const std::vector<int64_t> &request) {
    transport().send(PilotFrame{Comm::rank(), Conf::IN_PATH_SWITCH_RANK, physicalTag, request});
}

std::vector<int64_t> RoutedComm::receive(int physicalTag) {
    return transport().receive(physicalTag).words;
}
