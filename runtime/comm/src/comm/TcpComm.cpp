#include "comm/TcpComm.h"

#include "comm/item/FutureRequestWrapper.h"
#include "comm/protocol/InPathBmtProtocol.h"
#include "comm/protocol/PilotWireMessageCodec.h"
#include "conf/Conf.h"
#include "intermediate/IntermediateDataSupport.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <future>
#include <netinet/in.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

namespace {
constexpr int64_t kHelloTag = -1;

int inPathPhysicalTag() {
    return Comm::TAG_SWITCH_LANE_BASE + IntermediateDataSupport::currentLane();
}

int peerRankFor(int rank) {
    if (rank == Comm::SERVER0_RANK) return Comm::SERVER1_RANK;
    if (rank == Comm::SERVER1_RANK) return Comm::SERVER0_RANK;
    throw std::runtime_error("Current rank is not a TCP server party.");
}

void closeFd(int &fd) {
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
}

void throwSystemError(const char *message) {
    throw std::runtime_error(std::string(message) + ": " + std::strerror(errno));
}

void writeAll(int fd, const void *data, size_t bytes) {
    const auto *ptr = static_cast<const char *>(data);
    while (bytes > 0) {
        const ssize_t written = ::send(fd, ptr, bytes, 0);
        if (written < 0) {
            if (errno == EINTR) continue;
            throwSystemError("TCP communication send failed");
        }
        ptr += written;
        bytes -= static_cast<size_t>(written);
    }
}

bool readAll(int fd, void *data, size_t bytes) {
    auto *ptr = static_cast<char *>(data);
    while (bytes > 0) {
        const ssize_t readBytes = recv(fd, ptr, bytes, 0);
        if (readBytes == 0) return false;
        if (readBytes < 0) {
            if (errno == EINTR) continue;
            throwSystemError("TCP communication receive failed");
        }
        ptr += readBytes;
        bytes -= static_cast<size_t>(readBytes);
    }
    return true;
}

int connectToSwitch() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) throwSystemError("TCP communication socket creation failed");

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(static_cast<uint16_t>(Conf::TCP_SWITCH_PORT));
    for (int attempt = 0; attempt < 300; ++attempt) {
        if (connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0 || errno == EISCONN) {
            return fd;
        }
        if (errno != ECONNREFUSED && errno != ENOENT) {
            close(fd);
            throwSystemError("TCP communication connect failed");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    close(fd);
    throw std::runtime_error("Timed out connecting to TCP software switch.");
}
}

int TcpComm::rank_() { return _rank; }

void TcpComm::init_(int argc, char **argv) {
    (void) argc;
    (void) argv;
    if (Conf::COMM_TYPE != Conf::TCP || Conf::SIMULATION_LEVEL != Conf::SIMULATION_SOFTWARE) {
        throw std::runtime_error("TcpComm requires comm=tcp and simulation_level=software.");
    }
    _rank = Conf::ENDPOINT_RANK;
    if (_rank == Conf::IN_PATH_SWITCH_RANK) return;
    _socketFd = connectToSwitch();
    sendMessage(PilotMessage{_rank, Conf::IN_PATH_SWITCH_RANK,
                             static_cast<int>(kHelloTag), CONTROL_MESSAGE, {}, {}});
    _receiveThread = std::thread(&TcpComm::receiveLoop, this);
}

void TcpComm::finalize_() {
    if (_finalized) return;
    _finalized = true;
    _inbox.close();
    if (_socketFd >= 0) shutdown(_socketFd, SHUT_RDWR);
    closeFd(_socketFd);
    if (_receiveThread.joinable()) _receiveThread.join();
}

bool TcpComm::isServer_() { return Comm::isServerRank(_rank); }
bool TcpComm::isClient_() { return _rank == CLIENT_RANK; }

void TcpComm::sendMessage(const PilotMessage &message) {
    if (_socketFd < 0) throw std::runtime_error("TcpComm is not initialized.");
    const auto bytes = PilotWireMessageCodec::encode(message);
    std::lock_guard<std::mutex> lock(_sendMutex);
    writeAll(_socketFd, bytes.data(), bytes.size());
}

bool TcpComm::receiveMessage(PilotMessage &message) {
    std::array<uint8_t, PilotWireMessageCodec::HEADER_SIZE> header{};
    if (!readAll(_socketFd, header.data(), header.size())) return false;
    size_t payloadBytes = 0;
    if (!PilotWireMessageCodec::payloadBytesFromHeader(header.data(), header.size(), payloadBytes)) {
        throw std::runtime_error("Invalid TcpComm wire header.");
    }
    std::vector<uint8_t> bytes(PilotWireMessageCodec::HEADER_SIZE + payloadBytes);
    std::copy(header.begin(), header.end(), bytes.begin());
    if (payloadBytes > 0 &&
        !readAll(_socketFd, bytes.data() + PilotWireMessageCodec::HEADER_SIZE, payloadBytes)) return false;
    return PilotWireMessageCodec::decode(bytes.data(), bytes.size(), message);
}

void TcpComm::receiveLoop() {
    try {
        while (!_finalized) {
            PilotMessage message;
            if (!receiveMessage(message)) return;
            _inbox.push(std::move(message));
        }
    } catch (...) {
        if (!_finalized) _inbox.close();
    }
}

void TcpComm::send_(const std::vector<int64_t> &source, int width, int receiverRank, int tag) {
    (void) width;
    sendMessage(PilotMessage{_rank, receiverRank, tag, VECTOR_MESSAGE, source, {}});
}
void TcpComm::send_(int64_t source, int width, int receiverRank, int tag) {
    (void) width;
    sendMessage(PilotMessage{_rank, receiverRank, tag, INT64_MESSAGE, {source}, {}});
}
void TcpComm::send_(const std::string &source, int receiverRank, int tag) {
    sendMessage(PilotMessage{_rank, receiverRank, tag, STRING_MESSAGE, {}, source});
}
void TcpComm::receive_(int64_t &source, int width, int senderRank, int tag) {
    (void) width;
    auto message = _inbox.wait(senderRank, tag, INT64_MESSAGE);
    if (message.words.empty()) throw std::runtime_error("Missing TCP scalar payload.");
    source = message.words[0];
}
void TcpComm::receive_(std::vector<int64_t> &source, int width, int senderRank, int tag) {
    (void) width;
    source = std::move(_inbox.wait(senderRank, tag, VECTOR_MESSAGE).words);
}
void TcpComm::receive_(std::string &target, int senderRank, int tag) {
    target = std::move(_inbox.wait(senderRank, tag, STRING_MESSAGE).text);
}

AbstractRequest *TcpComm::sendAsync_(const std::vector<int64_t> &source, int width, int receiverRank, int tag) {
    return new FutureRequestWrapper(std::async(std::launch::async, [=, this] { send_(source, width, receiverRank, tag); }));
}
AbstractRequest *TcpComm::sendAsync_(const int64_t &source, int width, int receiverRank, int tag) {
    return new FutureRequestWrapper(std::async(std::launch::async, [=, this] { send_(source, width, receiverRank, tag); }));
}
AbstractRequest *TcpComm::sendAsync_(const std::string &source, int receiverRank, int tag) {
    return new FutureRequestWrapper(std::async(std::launch::async, [=, this] { send_(source, receiverRank, tag); }));
}
AbstractRequest *TcpComm::receiveAsync_(int64_t &target, int width, int senderRank, int tag) {
    return new FutureRequestWrapper(std::async(std::launch::async, [&target, width, senderRank, tag, this] { receive_(target, width, senderRank, tag); }));
}
AbstractRequest *TcpComm::receiveAsync_(std::vector<int64_t> &target, int count, int width, int senderRank, int tag) {
    return new FutureRequestWrapper(std::async(std::launch::async, [&target, count, width, senderRank, tag, this] {
        receive_(target, width, senderRank, tag);
        if (count >= 0 && static_cast<int>(target.size()) > count) target.resize(count);
    }));
}
AbstractRequest *TcpComm::receiveAsync_(std::string &target, int length, int senderRank, int tag) {
    return new FutureRequestWrapper(std::async(std::launch::async, [&target, length, senderRank, tag, this] {
        receive_(target, senderRank, tag);
        if (length >= 0 && static_cast<int>(target.size()) > length) target.resize(length);
    }));
}

void TcpComm::serverSendImpl_(const int64_t &source, int width, int tag) {
    if (!isServer_()) return Comm::serverSendImpl_(source, width, tag);
    serverSendImpl_(std::vector<int64_t>{source}, width, tag);
}
void TcpComm::serverSendImpl_(const std::vector<int64_t> &source, int width, int tag) {
    (void) width;
    if (!isServer_()) return Comm::serverSendImpl_(source, width, tag);
    auto request = InPathBmtProtocol::makeRequest(
        tag, IntermediateDataSupport::consumeBitwiseBmtRequestCount(), source);
    sendMessage(PilotMessage{_rank, peerRankFor(_rank), inPathPhysicalTag(), VECTOR_MESSAGE,
                             std::move(request), {}});
}
void TcpComm::serverReceiveImpl_(int64_t &source, int width, int tag) {
    if (!isServer_()) return Comm::serverReceiveImpl_(source, width, tag);
    std::vector<int64_t> payload;
    serverReceiveImpl_(payload, width, tag);
    if (payload.empty()) throw std::runtime_error("Missing scalar payload in TCP switch envelope.");
    source = payload[0];
}
void TcpComm::serverReceiveImpl_(std::vector<int64_t> &source, int width, int tag) {
    (void) width;
    if (!isServer_()) return Comm::serverReceiveImpl_(source, width, tag);
    auto envelope = _inbox.wait(peerRankFor(_rank), inPathPhysicalTag(), VECTOR_MESSAGE).words;
    source = InPathBmtProtocol::unpackPayload(envelope);
}

AbstractRequest *TcpComm::serverSendAsyncImpl_(const int64_t &source, int width, int tag) {
    return serverSendAsyncImpl_(std::vector<int64_t>{source}, width, tag);
}
AbstractRequest *TcpComm::serverSendAsyncImpl_(const std::vector<int64_t> &source, int width, int tag) {
    if (!isServer_()) return Comm::serverSendAsyncImpl_(source, width, tag);
    auto request = InPathBmtProtocol::makeRequest(
        tag, IntermediateDataSupport::consumeBitwiseBmtRequestCount(), source);
    const int peer = peerRankFor(_rank);
    const int physicalTag = inPathPhysicalTag();
    return new FutureRequestWrapper(std::async(std::launch::async,
        [this, peer, physicalTag, request = std::move(request)] {
            sendMessage(PilotMessage{_rank, peer, physicalTag, VECTOR_MESSAGE, request, {}});
        }));
}
AbstractRequest *TcpComm::serverReceiveAsyncImpl_(int64_t &target, int width, int tag) {
    if (!isServer_()) return Comm::serverReceiveAsyncImpl_(target, width, tag);
    const int peer = peerRankFor(_rank);
    const int physicalTag = inPathPhysicalTag();
    return new FutureRequestWrapper(std::async(std::launch::async, [&target, peer, physicalTag, this] {
        auto payload = InPathBmtProtocol::unpackPayload(
            _inbox.wait(peer, physicalTag, VECTOR_MESSAGE).words);
        if (payload.empty()) throw std::runtime_error("Missing scalar payload in TCP switch envelope.");
        target = payload[0];
    }));
}
AbstractRequest *TcpComm::serverReceiveAsyncImpl_(std::vector<int64_t> &target, int count, int width, int tag) {
    if (!isServer_()) return Comm::serverReceiveAsyncImpl_(target, count, width, tag);
    const int peer = peerRankFor(_rank);
    const int physicalTag = inPathPhysicalTag();
    return new FutureRequestWrapper(std::async(std::launch::async, [&target, count, peer, physicalTag, this] {
        target = InPathBmtProtocol::unpackPayload(_inbox.wait(peer, physicalTag, VECTOR_MESSAGE).words);
        if (count >= 0 && static_cast<int>(target.size()) > count) target.resize(count);
    }));
}
