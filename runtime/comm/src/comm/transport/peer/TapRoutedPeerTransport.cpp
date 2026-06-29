#include "comm/transport/peer/TapRoutedPeerTransport.h"

#include "comm/Comm.h"
#include "comm/transport/peer/TapFrameCodec.h"
#include "conf/Conf.h"

#include <array>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifdef __linux__
#include <arpa/inet.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {
std::string interfaceForRank(int rank) {
    if (rank == Comm::SERVER0_RANK) {
        return Conf::ROUTED_TAP_SERVER0;
    }
    if (rank == Comm::SERVER1_RANK) {
        return Conf::ROUTED_TAP_SERVER1;
    }
    if (rank == Comm::CLIENT_RANK) {
        return Conf::ROUTED_TAP_CLIENT;
    }
    throw std::runtime_error("No TAP interface configured for routed rank " + std::to_string(rank) + ".");
}

void throwSystemError(const std::string &message) {
    throw std::runtime_error(message + ": " + std::strerror(errno));
}

#ifdef __linux__
int openRawInterface(const std::string &name) {
    const int fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (fd < 0) {
        throwSystemError("TAP raw socket creation failed");
    }

    const unsigned int ifindex = if_nametoindex(name.c_str());
    if (ifindex == 0) {
        close(fd);
        throwSystemError("TAP interface lookup failed for " + name);
    }

    sockaddr_ll bindAddress{};
    bindAddress.sll_family = AF_PACKET;
    bindAddress.sll_protocol = htons(ETH_P_ALL);
    bindAddress.sll_ifindex = static_cast<int>(ifindex);
    if (bind(fd, reinterpret_cast<sockaddr *>(&bindAddress), sizeof(bindAddress)) != 0) {
        close(fd);
        throwSystemError("TAP raw socket bind failed for " + name);
    }
    return fd;
}

void closeFd(int &fd) {
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
}
#endif
}

void TapRoutedPeerTransport::init(int rank, MessageHandler handler) {
    _rank = rank;
    _handler = std::move(handler);
    _interfaceName = interfaceForRank(rank);
#ifdef __linux__
    _fd = openRawInterface(_interfaceName);
    _receiveThread = std::thread(&TapRoutedPeerTransport::receiveLoop, this);
#else
    throw std::runtime_error(
        "routed_network=tap currently requires Linux AF_PACKET raw sockets. "
        "Configure TAP/veth on Linux, or keep using routed_network=tcp for software simulation.");
#endif
}

void TapRoutedPeerTransport::send(const RoutedPeerMessage &message) {
#ifdef __linux__
    if (_fd < 0) {
        throw std::runtime_error("TAP routed peer transport is not initialized.");
    }
    auto frame = TapFrameCodec::encode(message);
    const auto dst = TapFrameCodec::macForRank(message.receiverRank);

    sockaddr_ll address{};
    address.sll_family = AF_PACKET;
    address.sll_ifindex = static_cast<int>(if_nametoindex(_interfaceName.c_str()));
    address.sll_halen = static_cast<unsigned char>(dst.size());
    std::memcpy(address.sll_addr, dst.data(), dst.size());

    const ssize_t written = sendto(_fd, frame.data(), frame.size(), 0,
                                   reinterpret_cast<sockaddr *>(&address), sizeof(address));
    if (written < 0 || static_cast<size_t>(written) != frame.size()) {
        throwSystemError("TAP frame send failed on " + _interfaceName);
    }
#else
    (void) message;
    throw std::runtime_error("TAP routed peer transport is only implemented on Linux.");
#endif
}

void TapRoutedPeerTransport::finalize() {
    if (_finalized) {
        return;
    }
    _finalized = true;
#ifdef __linux__
    closeFd(_fd);
    if (_receiveThread.joinable()) {
        _receiveThread.join();
    }
#endif
}

void TapRoutedPeerTransport::receiveLoop() {
#ifdef __linux__
    std::array<uint8_t, 65536> buffer{};
    while (!_finalized) {
        sockaddr_ll source{};
        socklen_t sourceLen = sizeof(source);
        const ssize_t readBytes = recvfrom(_fd, buffer.data(), buffer.size(), 0,
                                           reinterpret_cast<sockaddr *>(&source), &sourceLen);
        if (readBytes < 0) {
            if (_finalized || errno == EBADF) {
                return;
            }
            if (errno == EINTR) {
                continue;
            }
            return;
        }
        if (source.sll_pkttype == PACKET_OUTGOING) {
            continue;
        }

        RoutedPeerMessage message;
        if (!TapFrameCodec::decode(buffer.data(), static_cast<size_t>(readBytes), message)) {
            continue;
        }
        if (message.receiverRank != _rank) {
            continue;
        }
        _handler(std::move(message));
    }
#endif
}
