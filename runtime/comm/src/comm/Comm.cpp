
#include <utility>

#include "comm/Comm.h"

#include <vector>

#include "comm/InPathSwitchSimulator.h"
#include "comm/MpiComm.h"
#include "comm/RoutedComm.h"
#include "comm/item/FutureRequestWrapper.h"
#include "conf/Conf.h"
#include "intermediate/IntermediateDataSupport.h"
#include "parallel/LaneThreadPool.h"
#include "utils/Log.h"

#include <chrono>
#include <future>
#include <stdexcept>
#include <string>

namespace {
int64_t currentTimeMillis() {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

int inPathPhysicalTag() {
    return Comm::TAG_SWITCH_LANE_BASE + IntermediateDataSupport::currentLane();
}
}

#define MEASURE_EXECUTION_TIME(statement) \
int64_t start = 0; \
if (Conf::ENABLE_CLASS_WISE_TIMING) { \
start = currentTimeMillis(); \
} \
statement; \
if (Conf::ENABLE_CLASS_WISE_TIMING) { \
_totalTime += currentTimeMillis() - start; \
}

int Comm::rank() {
    return impl->rank_();
}

void Comm::init(int argc, char **argv) {
    if (Conf::COMM_TYPE == Conf::MPI) {
        impl = new MpiComm();
    } else if (Conf::COMM_TYPE == Conf::ROUTED) {
        impl = new RoutedComm();
    } else {
        throw std::runtime_error("Pilot only supports MPI or routed communication.");
    }
    impl->init_(argc, argv);
}

void Comm::finalize() {
    impl->finalize_();
    LaneThreadPool::finalize();
}

bool Comm::isServer() {
    return impl->isServer_();
}

bool Comm::isClient() {
    return impl->isClient_();
}

bool Comm::isServerRank(int rank) {
    return rank == SERVER0_RANK || rank == SERVER1_RANK;
}

int Comm::serverPeerRank() {
    if (rank() == SERVER0_RANK) {
        return SERVER1_RANK;
    }
    if (rank() == SERVER1_RANK) {
        return SERVER0_RANK;
    }
    throw std::runtime_error("Current rank is not a 2PC server party.");
}

void Comm::serverSend(const int64_t &source, int width, int tag) {
    impl->serverSendImpl_(source, width, tag);
}

void Comm::serverSend(const std::vector<int64_t> &source, int width, int tag) {
    impl->serverSendImpl_(source, width, tag);
}

void Comm::serverSend(const std::string &source, int tag) {
    impl->serverSendImpl_(source, tag);
}

void Comm::serverSend(const int64_t &source, int width) {
    serverSend(source, width, inPathPhysicalTag());
}

void Comm::serverSend(const std::vector<int64_t> &source, int width) {
    serverSend(source, width, inPathPhysicalTag());
}

void Comm::serverReceive(int64_t &source, int width, int tag) {
    impl->serverReceiveImpl_(source, width, tag);
}

void Comm::serverReceive(std::vector<int64_t> &source, int width, int tag) {
    impl->serverReceiveImpl_(source, width, tag);
}

void Comm::serverReceive(std::string &target, int tag) {
    impl->serverReceiveImpl_(target, tag);
}

void Comm::serverReceive(int64_t &source, int width) {
    serverReceive(source, width, inPathPhysicalTag());
}

void Comm::serverReceive(std::vector<int64_t> &source, int width) {
    serverReceive(source, width, inPathPhysicalTag());
}

void Comm::serverSendImpl_(const int64_t &source, int width, int tag) {
    if (InPathSwitchSimulator::shouldRouteServerTraffic()) {
        const int physicalTag = inPathPhysicalTag();
        std::vector<int64_t> payload{source};
        auto request = InPathSwitchSimulator::makeSwitchRequest(
            tag, IntermediateDataSupport::consumeBitwiseBmtRequestCount(), payload);
        MEASURE_EXECUTION_TIME(send(request, 64, Conf::IN_PATH_SWITCH_RANK, physicalTag));
        return;
    }
    try {
        MEASURE_EXECUTION_TIME(send(source, width, serverPeerRank(), tag));
    } catch (...) {}
}

void Comm::serverSendImpl_(const std::vector<int64_t> &source, int width, int tag) {
    if (InPathSwitchSimulator::shouldRouteServerTraffic()) {
        const int physicalTag = inPathPhysicalTag();
        auto request = InPathSwitchSimulator::makeSwitchRequest(
            tag, IntermediateDataSupport::consumeBitwiseBmtRequestCount(), source);
        MEASURE_EXECUTION_TIME(send(request, 64, Conf::IN_PATH_SWITCH_RANK, physicalTag));
        return;
    }
    try {
        MEASURE_EXECUTION_TIME(send(source, width, serverPeerRank(), tag));
    } catch (...) {}
}

void Comm::serverSendImpl_(const std::string &source, int tag) {
    try {
        MEASURE_EXECUTION_TIME(send(source, serverPeerRank(), tag));
    } catch (...) {}
}

void Comm::serverReceiveImpl_(int64_t &source, int width, int tag) {
    if (InPathSwitchSimulator::shouldRouteServerTraffic()) {
        const int physicalTag = inPathPhysicalTag();
        std::vector<int64_t> envelope;
        MEASURE_EXECUTION_TIME(receive(envelope, 64, Conf::IN_PATH_SWITCH_RANK, physicalTag));
        auto payload = InPathSwitchSimulator::unpackPayload(envelope);
        if (payload.empty()) {
            throw std::runtime_error("Missing scalar payload in in-path switch envelope.");
        }
        source = payload[0];
        return;
    }
    try {
        MEASURE_EXECUTION_TIME(receive(source, width, serverPeerRank(), tag));
    } catch (...) {}
}

void Comm::serverReceiveImpl_(std::vector<int64_t> &source, int width, int tag) {
    if (InPathSwitchSimulator::shouldRouteServerTraffic()) {
        const int physicalTag = inPathPhysicalTag();
        std::vector<int64_t> envelope;
        MEASURE_EXECUTION_TIME(receive(envelope, 64, Conf::IN_PATH_SWITCH_RANK, physicalTag));
        source = InPathSwitchSimulator::unpackPayload(envelope);
        return;
    }
    try {
        MEASURE_EXECUTION_TIME(receive(source, width, serverPeerRank(), tag));
    } catch (...) {}
}

void Comm::serverReceiveImpl_(std::string &target, int tag) {
    try {
        MEASURE_EXECUTION_TIME(receive(target, serverPeerRank(), tag));
    } catch (...) {}
}

void Comm::send(const int64_t &source, int width, int receiverRank, int tag) {
    try {
        MEASURE_EXECUTION_TIME(impl->send_(source, width, receiverRank, tag));
    } catch (...) {}
}

void Comm::send(const std::vector<int64_t> &source, int width, int receiverRank, int tag) {
    try {
        MEASURE_EXECUTION_TIME(impl->send_(source, width, receiverRank, tag));
    } catch (...) {}
}

void Comm::send(const std::string &source, int receiverRank, int tag) {
    try {
        MEASURE_EXECUTION_TIME(impl->send_(source, receiverRank, tag));
    } catch (...) {}
}

void Comm::receive(int64_t &source, int width, int senderRank, int tag) {
    try {
        MEASURE_EXECUTION_TIME(impl->receive_(source, width, senderRank, tag));
    } catch (...) {}
}

void Comm::receive(std::vector<int64_t> &source, int width, int senderRank, int tag) {
    try {
        MEASURE_EXECUTION_TIME(impl->receive_(source, width, senderRank, tag));
    } catch (...) {}
}

void Comm::receive(std::string &target, int senderRank, int tag) {
    try {
        MEASURE_EXECUTION_TIME(impl->receive_(target, senderRank, tag));
    } catch (...) {}
}

AbstractRequest *Comm::receiveAsync(int64_t &source, int width, int senderRank, int tag) {
    try {
        return impl->receiveAsync_(source, width, senderRank, tag);
    } catch (...) {
        return nullptr;
    }
}

AbstractRequest *Comm::receiveAsync(std::vector<int64_t> &source, int count, int width, int senderRank, int tag) {
    try {
        return impl->receiveAsync_(source, count, width, senderRank, tag);
    } catch (...) {
        return nullptr;
    }
}

AbstractRequest *Comm::receiveAsync(std::string &target, int length, int senderRank, int tag) {
    try {
        return impl->receiveAsync_(target, length, senderRank, tag);
    } catch (...) {
        return nullptr;
    }
}

AbstractRequest *Comm::sendAsync(const std::vector<int64_t> &source, int width, int receiverRank, int tag) {
    try {
        return impl->sendAsync_(source, width, receiverRank, tag);
    } catch (...) {
        return nullptr;
    }
}

AbstractRequest *Comm::sendAsync(const int64_t &source, int width, int receiverRank, int tag) {
    try {
        return impl->sendAsync_(source, width, receiverRank, tag);
    } catch (...) {
        return nullptr;
    }
}

AbstractRequest *Comm::sendAsync(const std::string &source, int receiverRank, int tag) {
    try {
        return impl->sendAsync_(source, receiverRank, tag);
    } catch (...) {
        return nullptr;
    }
}

AbstractRequest *Comm::serverSendAsync(const int64_t &source, int width, int tag) {
    return impl->serverSendAsyncImpl_(source, width, tag);
}

AbstractRequest *Comm::serverSendAsync(const std::vector<int64_t> &source, int width, int tag) {
    return impl->serverSendAsyncImpl_(source, width, tag);
}

AbstractRequest *Comm::serverSendAsync(const std::string &source, int tag) {
    return impl->serverSendAsyncImpl_(source, tag);
}

AbstractRequest *Comm::serverSendAsync(const int64_t &source, int width) {
    return serverSendAsync(source, width, inPathPhysicalTag());
}

AbstractRequest *Comm::serverSendAsync(const std::vector<int64_t> &source, int width) {
    return serverSendAsync(source, width, inPathPhysicalTag());
}

AbstractRequest *Comm::serverReceiveAsync(int64_t &target, int width, int tag) {
    return impl->serverReceiveAsyncImpl_(target, width, tag);
}

AbstractRequest *Comm::serverReceiveAsync(std::vector<int64_t> &target, int count, int width, int tag) {
    return impl->serverReceiveAsyncImpl_(target, count, width, tag);
}

AbstractRequest *Comm::serverReceiveAsync(std::string &target, int length, int tag) {
    return impl->serverReceiveAsyncImpl_(target, length, tag);
}

AbstractRequest *Comm::serverReceiveAsync(int64_t &target, int width) {
    return serverReceiveAsync(target, width, inPathPhysicalTag());
}

AbstractRequest *Comm::serverReceiveAsync(std::vector<int64_t> &target, int count, int width) {
    return serverReceiveAsync(target, count, width, inPathPhysicalTag());
}

AbstractRequest *Comm::serverSendAsyncImpl_(const int64_t &source, int width, int tag) {
    if (InPathSwitchSimulator::shouldRouteServerTraffic()) {
        const int physicalTag = inPathPhysicalTag();
        std::vector<int64_t> payload{source};
        auto request = InPathSwitchSimulator::makeSwitchRequest(
            tag, IntermediateDataSupport::consumeBitwiseBmtRequestCount(), payload);
        return sendAsync(request, 64, Conf::IN_PATH_SWITCH_RANK, physicalTag);
    }
    try {
        return sendAsync(source, width, serverPeerRank(), tag);
    } catch (...) {
        return nullptr;
    }
}

AbstractRequest *Comm::serverSendAsyncImpl_(const std::vector<int64_t> &source, int width, int tag) {
    if (InPathSwitchSimulator::shouldRouteServerTraffic()) {
        const int physicalTag = inPathPhysicalTag();
        auto request = InPathSwitchSimulator::makeSwitchRequest(
            tag, IntermediateDataSupport::consumeBitwiseBmtRequestCount(), source);
        return sendAsync(request, 64, Conf::IN_PATH_SWITCH_RANK, physicalTag);
    }
    try {
        return sendAsync(source, width, serverPeerRank(), tag);
    } catch (...) {
        return nullptr;
    }
}

AbstractRequest *Comm::serverSendAsyncImpl_(const std::string &source, int tag) {
    try {
        return sendAsync(source, serverPeerRank(), tag);
    } catch (...) {
        return nullptr;
    }
}

AbstractRequest *Comm::serverReceiveAsyncImpl_(int64_t &target, int width, int tag) {
    if (InPathSwitchSimulator::shouldRouteServerTraffic()) {
        const int physicalTag = inPathPhysicalTag();
        return new FutureRequestWrapper(std::async(std::launch::async, [&target, physicalTag]() {
            std::vector<int64_t> envelope;
            receive(envelope, 64, Conf::IN_PATH_SWITCH_RANK, physicalTag);
            auto payload = InPathSwitchSimulator::unpackPayload(envelope);
            if (payload.empty()) {
                throw std::runtime_error("Missing scalar payload in in-path switch envelope.");
            }
            target = payload[0];
        }));
    }
    try {
        return receiveAsync(target, width, serverPeerRank(), tag);
    } catch (...) {
        return nullptr;
    }
}

AbstractRequest *Comm::serverReceiveAsyncImpl_(std::vector<int64_t> &target, int count, int width, int tag) {
    if (InPathSwitchSimulator::shouldRouteServerTraffic()) {
        const int physicalTag = inPathPhysicalTag();
        return new FutureRequestWrapper(std::async(std::launch::async, [&target, count, physicalTag]() {
            std::vector<int64_t> envelope;
            receive(envelope, 64, Conf::IN_PATH_SWITCH_RANK, physicalTag);
            auto payload = InPathSwitchSimulator::unpackPayload(envelope);
            if (count >= 0 && static_cast<int>(payload.size()) > count) {
                payload.resize(count);
            }
            target = std::move(payload);
        }));
    }
    try {
        return receiveAsync(target, count, width, serverPeerRank(), tag);
    } catch (...) {
        return nullptr;
    }
}

AbstractRequest *Comm::serverReceiveAsyncImpl_(std::string &target, int length, int tag) {
    try {
        return receiveAsync(target, length, serverPeerRank(), tag);
    } catch (...) {
        return nullptr;
    }
}

void Comm::wait(AbstractRequest *request) {
    try {
        request->wait();
        delete request;
    } catch (const std::exception &e) {
        Log::e(e.what());
    } catch (...) {
        Log::e("Unknown async communication error.");
    }
}
