#include "comm/Comm.h"
#include "comm/InPathSwitchSimulator.h"
#include "comm/protocol/PilotPacket.h"
#include "comm/transport/peer/RoutedPeerTransport.h"
#include "comm/transport/peer/TapFrameCodec.h"
#include "utils/Log.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef __linux__
#include <arpa/inet.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {
std::string getStringArg(int argc, char **argv, const std::string &name, const std::string &fallback) {
    const std::string prefix = "--" + name + "=";
    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if (arg.rfind(prefix, 0) == 0) {
            return arg.substr(prefix.size());
        }
    }
    return fallback;
}

bool isServerRank(int rank) {
    return rank == Comm::SERVER0_RANK || rank == Comm::SERVER1_RANK;
}

int peerServerRank(int rank) {
    return rank == Comm::SERVER0_RANK ? Comm::SERVER1_RANK : Comm::SERVER0_RANK;
}

bool isServerSwitchRequest(const RoutedPeerMessage &message) {
    return isServerRank(message.senderRank) &&
           isServerRank(message.receiverRank) &&
           message.type == 2 &&
           !message.words.empty() &&
           message.words[0] == PilotPacket::REQUEST_MAGIC;
}

RoutedPeerMessage transformServerRequest(const RoutedPeerMessage &message) {
    RoutedPeerMessage out = message;
    out.receiverRank = peerServerRank(message.senderRank);
    out.words = InPathSwitchSimulator::forwardRequest(message.senderRank, message.tag, message.words);
    return out;
}

#ifdef __linux__
void throwSystemError(const std::string &message) {
    throw std::runtime_error(message + ": " + std::strerror(errno));
}

int openRawInterface(const std::string &name) {
    const int fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (fd < 0) {
        throwSystemError("tap switch raw socket creation failed");
    }

    const unsigned int ifindex = if_nametoindex(name.c_str());
    if (ifindex == 0) {
        close(fd);
        throwSystemError("tap switch interface lookup failed for " + name);
    }

    sockaddr_ll bindAddress{};
    bindAddress.sll_family = AF_PACKET;
    bindAddress.sll_protocol = htons(ETH_P_ALL);
    bindAddress.sll_ifindex = static_cast<int>(ifindex);
    if (bind(fd, reinterpret_cast<sockaddr *>(&bindAddress), sizeof(bindAddress)) != 0) {
        close(fd);
        throwSystemError("tap switch raw socket bind failed for " + name);
    }
    return fd;
}

void closeFd(int &fd) {
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
}

void sendFrame(int fd, const RoutedPeerMessage &message, const std::string &interfaceName) {
    auto frame = TapFrameCodec::encode(message);
    const auto dst = TapFrameCodec::macForRank(message.receiverRank);

    sockaddr_ll address{};
    address.sll_family = AF_PACKET;
    address.sll_ifindex = static_cast<int>(if_nametoindex(interfaceName.c_str()));
    address.sll_halen = static_cast<unsigned char>(dst.size());
    std::memcpy(address.sll_addr, dst.data(), dst.size());
    const ssize_t written = sendto(fd, frame.data(), frame.size(), 0,
                                   reinterpret_cast<sockaddr *>(&address), sizeof(address));
    if (written < 0 || static_cast<size_t>(written) != frame.size()) {
        throwSystemError("tap switch frame send failed on " + interfaceName);
    }
}

struct Port {
    int rank{};
    std::string interfaceName;
    int fd{-1};
};

void runSwitch(std::array<Port, 3> &ports) {
    std::array<int, 3> fdByRank{-1, -1, -1};
    std::array<std::string, 3> interfaceByRank{};
    for (auto &port: ports) {
        port.fd = openRawInterface(port.interfaceName);
        fdByRank[static_cast<size_t>(port.rank)] = port.fd;
        interfaceByRank[static_cast<size_t>(port.rank)] = port.interfaceName;
    }

    Log::i("pilot TAP switch started.");
    std::array<uint8_t, 65536> buffer{};
    while (true) {
        fd_set readSet;
        FD_ZERO(&readSet);
        int maxFd = -1;
        for (const auto &port: ports) {
            FD_SET(port.fd, &readSet);
            maxFd = std::max(maxFd, port.fd);
        }

        const int ready = select(maxFd + 1, &readSet, nullptr, nullptr, nullptr);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            throwSystemError("tap switch select failed");
        }

        for (const auto &port: ports) {
            if (!FD_ISSET(port.fd, &readSet)) {
                continue;
            }
            sockaddr_ll source{};
            socklen_t sourceLen = sizeof(source);
            const ssize_t readBytes = recvfrom(port.fd, buffer.data(), buffer.size(), 0,
                                               reinterpret_cast<sockaddr *>(&source), &sourceLen);
            if (readBytes < 0) {
                if (errno == EINTR) {
                    continue;
                }
                throwSystemError("tap switch frame receive failed on " + port.interfaceName);
            }
            if (source.sll_pkttype == PACKET_OUTGOING) {
                continue;
            }

            RoutedPeerMessage message;
            if (!TapFrameCodec::decode(buffer.data(), static_cast<size_t>(readBytes), message)) {
                continue;
            }
            if (message.receiverRank < 0 || message.receiverRank >= static_cast<int>(fdByRank.size())) {
                continue;
            }

            RoutedPeerMessage out = isServerSwitchRequest(message) ? transformServerRequest(message) : message;
            sendFrame(fdByRank[static_cast<size_t>(out.receiverRank)], out,
                      interfaceByRank[static_cast<size_t>(out.receiverRank)]);
        }
    }
}
#endif
}

int main(int argc, char **argv) {
#ifndef __linux__
    (void) argc;
    (void) argv;
    Log::e("pilot_tap_switch currently requires Linux AF_PACKET raw sockets.");
    return 1;
#else
    std::array<Port, 3> ports{
        Port{Comm::SERVER0_RANK, getStringArg(argc, argv, "tap_server0", "tap-sw0")},
        Port{Comm::SERVER1_RANK, getStringArg(argc, argv, "tap_server1", "tap-sw1")},
        Port{Comm::CLIENT_RANK, getStringArg(argc, argv, "tap_client", "tap-sw2")}
    };

    try {
        runSwitch(ports);
    } catch (const std::exception &e) {
        Log::e(e.what());
        for (auto &port: ports) {
            closeFd(port.fd);
        }
        return 1;
    }
    return 0;
#endif
}
