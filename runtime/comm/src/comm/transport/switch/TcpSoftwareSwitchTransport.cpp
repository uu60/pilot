#include "comm/transport/switch/TcpSoftwareSwitchTransport.h"

#include "comm/Comm.h"
#include "comm/InPathSwitchSimulator.h"
#include "comm/protocol/PilotPacket.h"
#include "conf/Conf.h"
#include "utils/Log.h"

#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <mutex>
#include <netinet/in.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <utility>

namespace {
constexpr int kListenBacklog = 16;
constexpr int kControlMessage = 0;
constexpr int kVectorMessage = 2;
constexpr int kStringMessage = 3;
constexpr int64_t kHelloTag = -1;

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
            if (errno == EINTR) {
                continue;
            }
            throwSystemError("TCP software switch send failed");
        }
        ptr += written;
        bytes -= static_cast<size_t>(written);
    }
}

bool readAll(int fd, void *data, size_t bytes) {
    auto *ptr = static_cast<char *>(data);
    while (bytes > 0) {
        const ssize_t readBytes = recv(fd, ptr, bytes, 0);
        if (readBytes == 0) {
            return false;
        }
        if (readBytes < 0) {
            if (errno == EINTR) {
                continue;
            }
            throwSystemError("TCP software switch receive failed");
        }
        ptr += readBytes;
        bytes -= static_cast<size_t>(readBytes);
    }
    return true;
}

int createClientSocket() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        throwSystemError("TCP software switch client socket creation failed");
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(static_cast<uint16_t>(Conf::TCP_SWITCH_PORT));

    for (int attempt = 0; attempt < 300; ++attempt) {
        if (connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0) {
            return fd;
        }
        if (errno == EISCONN) {
            return fd;
        }
        if (errno != ECONNREFUSED && errno != ENOENT) {
            close(fd);
            throwSystemError("TCP software switch connect failed");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    close(fd);
    throw std::runtime_error("Timed out connecting to TCP software switch.");
}

int createServerSocket() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        throwSystemError("TCP software switch server socket creation failed");
    }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(static_cast<uint16_t>(Conf::TCP_SWITCH_PORT));
    if (bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) {
        close(fd);
        throwSystemError("TCP software switch bind failed");
    }
    if (listen(fd, kListenBacklog) != 0) {
        close(fd);
        throwSystemError("TCP software switch listen failed");
    }
    return fd;
}

void sendMessage(int fd, const RoutedPeerMessage &message) {
    const int64_t size = message.type == kStringMessage ? static_cast<int64_t>(message.text.size())
                                                        : static_cast<int64_t>(message.words.size());
    const int64_t header[5]{
        static_cast<int64_t>(message.senderRank),
        static_cast<int64_t>(message.receiverRank),
        static_cast<int64_t>(message.tag),
        static_cast<int64_t>(message.type),
        size
    };
    writeAll(fd, header, sizeof(header));
    if (message.type == kStringMessage && !message.text.empty()) {
        writeAll(fd, message.text.data(), message.text.size());
    } else if (message.type != kStringMessage && !message.words.empty()) {
        writeAll(fd, message.words.data(), message.words.size() * sizeof(int64_t));
    }
}

bool receiveMessage(int fd, RoutedPeerMessage &message) {
    int64_t header[5]{};
    if (!readAll(fd, header, sizeof(header))) {
        return false;
    }
    if (header[4] < 0) {
        throw std::runtime_error("Invalid TCP software switch message size.");
    }

    message.senderRank = static_cast<int>(header[0]);
    message.receiverRank = static_cast<int>(header[1]);
    message.tag = static_cast<int>(header[2]);
    message.type = static_cast<int>(header[3]);
    const auto size = static_cast<size_t>(header[4]);
    message.words.clear();
    message.text.clear();

    if (message.type == kStringMessage) {
        message.text.resize(size);
        return size == 0 || readAll(fd, message.text.data(), size);
    }

    message.words.resize(size);
    return size == 0 || readAll(fd, message.words.data(), size * sizeof(int64_t));
}

RoutedPeerMessage makeHello(int rank) {
    return RoutedPeerMessage{rank, Conf::IN_PATH_SWITCH_RANK, static_cast<int>(kHelloTag), kControlMessage, {}, {}};
}

bool isValidPilotRank(int rank) {
    return rank == Comm::SERVER0_RANK || rank == Comm::SERVER1_RANK || rank == Comm::CLIENT_RANK;
}

int peerServerRank(int rank) {
    return rank == Comm::SERVER0_RANK ? Comm::SERVER1_RANK : Comm::SERVER0_RANK;
}

bool isServerSwitchRequest(const RoutedPeerMessage &message) {
    return Comm::isServerRank(message.senderRank) &&
           Comm::isServerRank(message.receiverRank) &&
           message.type == kVectorMessage &&
           !message.words.empty() &&
           message.words[0] == PilotPacket::REQUEST_MAGIC;
}

RoutedPeerMessage maybeAppendBmt(const RoutedPeerMessage &message) {
    if (!isServerSwitchRequest(message)) {
        return message;
    }
    RoutedPeerMessage out = message;
    out.receiverRank = peerServerRank(message.senderRank);
    out.words = InPathSwitchSimulator::forwardRequest(message.senderRank, message.tag, message.words);
    return out;
}
}

void TcpSoftwareSwitchTransport::init(int rank, MessageHandler handler) {
    _rank = rank;
    _handler = std::move(handler);
    _socketFd = createClientSocket();
    sendMessage(_socketFd, makeHello(_rank));
    _receiveThread = std::thread(&TcpSoftwareSwitchTransport::receiveLoop, this);
}

void TcpSoftwareSwitchTransport::send(const RoutedPeerMessage &message) {
    if (_socketFd < 0) {
        throw std::runtime_error("TCP software switch transport is not initialized.");
    }
    std::lock_guard<std::mutex> lock(_sendMutex);
    sendMessage(_socketFd, message);
}

void TcpSoftwareSwitchTransport::finalize() {
    if (_finalized) {
        return;
    }
    _finalized = true;
    if (_socketFd >= 0) {
        shutdown(_socketFd, SHUT_RDWR);
    }
    closeFd(_socketFd);
    if (_receiveThread.joinable()) {
        _receiveThread.join();
    }
}

void TcpSoftwareSwitchTransport::receiveLoop() {
    try {
        while (!_finalized) {
            RoutedPeerMessage message;
            if (!receiveMessage(_socketFd, message)) {
                return;
            }
            _handler(std::move(message));
        }
    } catch (...) {
        // The owning Comm instance closes the socket during finalization to unblock this loop.
    }
}

void TcpSoftwareSwitchTransport::runSwitch() {
    Log::i("TCP in-path BMT software switch started.");
    int listenFd = createServerSocket();
    std::array<int, 3> fdByRank{-1, -1, -1};

    while (fdByRank[Comm::SERVER0_RANK] < 0 || fdByRank[Comm::SERVER1_RANK] < 0 ||
           fdByRank[Comm::CLIENT_RANK] < 0) {
        int fd = accept(listenFd, nullptr, nullptr);
        if (fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            closeFd(listenFd);
            throwSystemError("TCP software switch accept failed");
        }

        RoutedPeerMessage hello;
        if (!receiveMessage(fd, hello) || hello.type != kControlMessage || hello.tag != kHelloTag ||
            !isValidPilotRank(hello.senderRank)) {
            close(fd);
            continue;
        }
        fdByRank[static_cast<size_t>(hello.senderRank)] = fd;
    }

    std::mutex forwardMutex;
    auto worker = [&](int srcRank) {
        try {
            while (true) {
                RoutedPeerMessage message;
                if (!receiveMessage(fdByRank[static_cast<size_t>(srcRank)], message)) {
                    break;
                }
                auto out = maybeAppendBmt(message);
                if (!isValidPilotRank(out.receiverRank)) {
                    continue;
                }
                std::lock_guard<std::mutex> lock(forwardMutex);
                sendMessage(fdByRank[static_cast<size_t>(out.receiverRank)], out);
            }
        } catch (...) {
            // A peer may close while another worker is forwarding during shutdown.
        }
    };

    std::thread server0(worker, Comm::SERVER0_RANK);
    std::thread server1(worker, Comm::SERVER1_RANK);
    std::thread client(worker, Comm::CLIENT_RANK);
    server0.join();
    server1.join();
    client.join();

    for (int &fd: fdByRank) {
        closeFd(fd);
    }
    closeFd(listenFd);
    Log::i("TCP in-path BMT software switch stopped.");
}
