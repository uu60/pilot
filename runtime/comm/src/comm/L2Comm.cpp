#include "comm/L2Comm.h"

#include "comm/item/FutureRequestWrapper.h"
#include "comm/protocol/EthernetFrameCodec.h"
#include "comm/protocol/InPathBmtProtocol.h"
#include "conf/Conf.h"
#include "intermediate/IntermediateDataSupport.h"
#include "utils/Log.h"

#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <future>
#include <stdexcept>
#include <utility>

#ifdef __linux__
#include <arpa/inet.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {
int inPathPhysicalTag() {
    return Comm::TAG_SWITCH_LANE_BASE + IntermediateDataSupport::currentLane();
}

int peerRankFor(int rank) {
    if (rank == Comm::SERVER0_RANK) return Comm::SERVER1_RANK;
    if (rank == Comm::SERVER1_RANK) return Comm::SERVER0_RANK;
    throw std::runtime_error("Current rank is not a L2 server party.");
}

std::string interfaceForRank(int rank) {
    if (rank == Comm::SERVER0_RANK) return Conf::L2_INTERFACE_SERVER0;
    if (rank == Comm::SERVER1_RANK) return Conf::L2_INTERFACE_SERVER1;
    if (rank == Comm::CLIENT_RANK) return Conf::L2_INTERFACE_CLIENT;
    throw std::runtime_error("No L2 interface configured for rank " + std::to_string(rank) + ".");
}

bool l2DebugEnabled() {
    const char *value = std::getenv("PILOT_TAP_DEBUG");
    return value != nullptr && std::string(value) != "0";
}

#ifdef __linux__
void closeFd(int &fd) {
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
}

void throwSystemError(const std::string &message) {
    throw std::runtime_error(message + ": " + std::strerror(errno));
}

int openRawInterface(const std::string &name) {
    const int fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (fd < 0) throwSystemError("L2 raw socket creation failed");
    constexpr int bufferBytes = 4 * 1024 * 1024;
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &bufferBytes, sizeof(bufferBytes));
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &bufferBytes, sizeof(bufferBytes));

    const unsigned int ifindex = if_nametoindex(name.c_str());
    if (ifindex == 0) {
        close(fd);
        throwSystemError("L2 interface lookup failed for " + name);
    }
    sockaddr_ll bindAddress{};
    bindAddress.sll_family = AF_PACKET;
    bindAddress.sll_protocol = htons(ETH_P_ALL);
    bindAddress.sll_ifindex = static_cast<int>(ifindex);
    if (bind(fd, reinterpret_cast<sockaddr *>(&bindAddress), sizeof(bindAddress)) != 0) {
        close(fd);
        throwSystemError("L2 interface bind failed for " + name);
    }
    return fd;
}
#endif
}

int L2Comm::rank_() { return _rank; }

void L2Comm::init_(int argc, char **argv) {
    (void) argc;
    (void) argv;
    if (Conf::COMM_TYPE != Conf::L2 || Conf::SIMULATION_LEVEL != Conf::SIMULATION_SIMULATOR) {
        throw std::runtime_error("L2Comm requires comm=l2 and simulation_level=simulator.");
    }
    _rank = Conf::ENDPOINT_RANK;
    _interfaceName = interfaceForRank(_rank);
#ifdef __linux__
    _fd = openRawInterface(_interfaceName);
    _receiveThread = std::thread(&L2Comm::receiveLoop, this);
#else
    throw std::runtime_error("L2Comm currently requires Linux AF_PACKET raw sockets.");
#endif
}

void L2Comm::finalize_() {
    if (_finalized) return;
    _finalized = true;
    _inbox.close();
#ifdef __linux__
    closeFd(_fd);
    if (_receiveThread.joinable()) _receiveThread.join();
#endif
}

bool L2Comm::isServer_() { return Comm::isServerRank(_rank); }
bool L2Comm::isClient_() { return _rank == CLIENT_RANK; }

void L2Comm::sendMessage(const PilotMessage &message) {
#ifdef __linux__
    if (_fd < 0) throw std::runtime_error("L2Comm is not initialized.");
    const auto frame = EthernetFrameCodec::encode(message);
    const auto dst = EthernetFrameCodec::macForRank(message.receiverRank);
    sockaddr_ll address{};
    address.sll_family = AF_PACKET;
    address.sll_protocol = htons(EthernetFrameCodec::ETHERTYPE);
    address.sll_ifindex = static_cast<int>(if_nametoindex(_interfaceName.c_str()));
    address.sll_halen = static_cast<unsigned char>(dst.size());
    std::memcpy(address.sll_addr, dst.data(), dst.size());
    std::lock_guard<std::mutex> lock(_sendMutex);
    const ssize_t written = sendto(_fd, frame.data(), frame.size(), 0,
                                   reinterpret_cast<sockaddr *>(&address), sizeof(address));
    if (written < 0 || static_cast<size_t>(written) != frame.size()) {
        throwSystemError("L2 frame send failed on " + _interfaceName);
    }
    if (l2DebugEnabled()) Log::i("l2 send rank=" + std::to_string(_rank) +
                                  " iface=" + _interfaceName +
                                  " dst_rank=" + std::to_string(message.receiverRank));
#else
    (void) message;
    throw std::runtime_error("L2Comm currently requires Linux.");
#endif
}

void L2Comm::receiveLoop() {
#ifdef __linux__
    std::array<uint8_t, 65536> buffer{};
    while (!_finalized) {
        sockaddr_ll source{};
        socklen_t sourceLen = sizeof(source);
        const ssize_t readBytes = recvfrom(_fd, buffer.data(), buffer.size(), 0,
                                           reinterpret_cast<sockaddr *>(&source), &sourceLen);
        if (readBytes < 0) {
            if (_finalized || errno == EBADF) return;
            if (errno == EINTR) continue;
            _inbox.close();
            return;
        }
        if (source.sll_pkttype == PACKET_OUTGOING) continue;
        PilotMessage message;
        if (!EthernetFrameCodec::decode(buffer.data(), static_cast<size_t>(readBytes), message)) continue;
        if (message.receiverRank != _rank) continue;
        _inbox.push(std::move(message));
    }
#endif
}

void L2Comm::send_(const std::vector<int64_t> &source, int width, int receiverRank, int tag) {
    (void) width;
    sendMessage(PilotMessage{_rank, receiverRank, tag, VECTOR_MESSAGE, source, {}});
}
void L2Comm::send_(int64_t source, int width, int receiverRank, int tag) {
    (void) width;
    sendMessage(PilotMessage{_rank, receiverRank, tag, INT64_MESSAGE, {source}, {}});
}
void L2Comm::send_(const std::string &source, int receiverRank, int tag) {
    sendMessage(PilotMessage{_rank, receiverRank, tag, STRING_MESSAGE, {}, source});
}
void L2Comm::receive_(int64_t &source, int width, int senderRank, int tag) {
    (void) width;
    auto message = _inbox.wait(senderRank, tag, INT64_MESSAGE);
    if (message.words.empty()) throw std::runtime_error("Missing L2 scalar payload.");
    source = message.words[0];
}
void L2Comm::receive_(std::vector<int64_t> &source, int width, int senderRank, int tag) {
    (void) width;
    source = std::move(_inbox.wait(senderRank, tag, VECTOR_MESSAGE).words);
}
void L2Comm::receive_(std::string &target, int senderRank, int tag) {
    target = std::move(_inbox.wait(senderRank, tag, STRING_MESSAGE).text);
}

AbstractRequest *L2Comm::sendAsync_(const std::vector<int64_t> &source, int width, int receiverRank, int tag) {
    return new FutureRequestWrapper(std::async(std::launch::async, [=, this] { send_(source, width, receiverRank, tag); }));
}
AbstractRequest *L2Comm::sendAsync_(const int64_t &source, int width, int receiverRank, int tag) {
    return new FutureRequestWrapper(std::async(std::launch::async, [=, this] { send_(source, width, receiverRank, tag); }));
}
AbstractRequest *L2Comm::sendAsync_(const std::string &source, int receiverRank, int tag) {
    return new FutureRequestWrapper(std::async(std::launch::async, [=, this] { send_(source, receiverRank, tag); }));
}
AbstractRequest *L2Comm::receiveAsync_(int64_t &target, int width, int senderRank, int tag) {
    return new FutureRequestWrapper(std::async(std::launch::async, [&target, width, senderRank, tag, this] { receive_(target, width, senderRank, tag); }));
}
AbstractRequest *L2Comm::receiveAsync_(std::vector<int64_t> &target, int count, int width, int senderRank, int tag) {
    return new FutureRequestWrapper(std::async(std::launch::async, [&target, count, width, senderRank, tag, this] {
        receive_(target, width, senderRank, tag);
        if (count >= 0 && static_cast<int>(target.size()) > count) target.resize(count);
    }));
}
AbstractRequest *L2Comm::receiveAsync_(std::string &target, int length, int senderRank, int tag) {
    return new FutureRequestWrapper(std::async(std::launch::async, [&target, length, senderRank, tag, this] {
        receive_(target, senderRank, tag);
        if (length >= 0 && static_cast<int>(target.size()) > length) target.resize(length);
    }));
}

void L2Comm::serverSendImpl_(const int64_t &source, int width, int tag) {
    if (!isServer_()) return Comm::serverSendImpl_(source, width, tag);
    serverSendImpl_(std::vector<int64_t>{source}, width, tag);
}
void L2Comm::serverSendImpl_(const std::vector<int64_t> &source, int width, int tag) {
    (void) width;
    if (!isServer_()) return Comm::serverSendImpl_(source, width, tag);
    auto request = InPathBmtProtocol::makeRequest(
        tag, IntermediateDataSupport::consumeBitwiseBmtRequestCount(), source);
    sendMessage(PilotMessage{_rank, peerRankFor(_rank), inPathPhysicalTag(), VECTOR_MESSAGE,
                             std::move(request), {}});
}
void L2Comm::serverReceiveImpl_(int64_t &source, int width, int tag) {
    if (!isServer_()) return Comm::serverReceiveImpl_(source, width, tag);
    std::vector<int64_t> payload;
    serverReceiveImpl_(payload, width, tag);
    if (payload.empty()) throw std::runtime_error("Missing scalar payload in L2 switch envelope.");
    source = payload[0];
}
void L2Comm::serverReceiveImpl_(std::vector<int64_t> &source, int width, int tag) {
    (void) width;
    if (!isServer_()) return Comm::serverReceiveImpl_(source, width, tag);
    auto envelope = _inbox.wait(peerRankFor(_rank), inPathPhysicalTag(), VECTOR_MESSAGE).words;
    source = InPathBmtProtocol::unpackPayload(envelope);
}

AbstractRequest *L2Comm::serverSendAsyncImpl_(const int64_t &source, int width, int tag) {
    return serverSendAsyncImpl_(std::vector<int64_t>{source}, width, tag);
}
AbstractRequest *L2Comm::serverSendAsyncImpl_(const std::vector<int64_t> &source, int width, int tag) {
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
AbstractRequest *L2Comm::serverReceiveAsyncImpl_(int64_t &target, int width, int tag) {
    if (!isServer_()) return Comm::serverReceiveAsyncImpl_(target, width, tag);
    const int peer = peerRankFor(_rank);
    const int physicalTag = inPathPhysicalTag();
    return new FutureRequestWrapper(std::async(std::launch::async, [&target, peer, physicalTag, this] {
        auto payload = InPathBmtProtocol::unpackPayload(
            _inbox.wait(peer, physicalTag, VECTOR_MESSAGE).words);
        if (payload.empty()) throw std::runtime_error("Missing scalar payload in L2 switch envelope.");
        target = payload[0];
    }));
}
AbstractRequest *L2Comm::serverReceiveAsyncImpl_(std::vector<int64_t> &target, int count, int width, int tag) {
    if (!isServer_()) return Comm::serverReceiveAsyncImpl_(target, count, width, tag);
    const int peer = peerRankFor(_rank);
    const int physicalTag = inPathPhysicalTag();
    return new FutureRequestWrapper(std::async(std::launch::async, [&target, count, peer, physicalTag, this] {
        target = InPathBmtProtocol::unpackPayload(_inbox.wait(peer, physicalTag, VECTOR_MESSAGE).words);
        if (count >= 0 && static_cast<int>(target.size()) > count) target.resize(count);
    }));
}
