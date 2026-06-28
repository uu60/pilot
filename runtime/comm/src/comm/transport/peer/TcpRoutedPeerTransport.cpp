#include "comm/transport/peer/TcpRoutedPeerTransport.h"

#include "conf/Conf.h"

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <netinet/in.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <utility>

namespace {
constexpr int kListenBacklog = 16;
constexpr int kStringMessage = 3;

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
            throwSystemError("routed socket send failed");
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
            throwSystemError("routed socket receive failed");
        }
        ptr += readBytes;
        bytes -= static_cast<size_t>(readBytes);
    }
    return true;
}

int portForRank(int rank) {
    return Conf::ROUTED_BASE_PORT + rank;
}

int createServerSocket(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        throwSystemError("routed server socket creation failed");
    }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(static_cast<uint16_t>(port));
    if (bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) {
        close(fd);
        throwSystemError("routed server socket bind failed");
    }
    if (listen(fd, kListenBacklog) != 0) {
        close(fd);
        throwSystemError("routed server socket listen failed");
    }
    return fd;
}

int connectToPort(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        throwSystemError("routed client socket creation failed");
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(static_cast<uint16_t>(port));

    for (int attempt = 0; attempt < 300; ++attempt) {
        if (connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0) {
            return fd;
        }
        if (errno == EISCONN) {
            return fd;
        }
        if (errno != ECONNREFUSED && errno != ENOENT) {
            close(fd);
            throwSystemError("routed socket connect failed");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    close(fd);
    throw std::runtime_error("Timed out connecting to routed rank.");
}
}

void TcpRoutedPeerTransport::init(int rank, MessageHandler handler) {
    _rank = rank;
    _handler = std::move(handler);
    _listenFd = createServerSocket(portForRank(_rank));
    _listenerThread = std::thread(&TcpRoutedPeerTransport::listenerLoop, this);
}

void TcpRoutedPeerTransport::send(const RoutedPeerMessage &message) {
    int fd = connectToPort(portForRank(message.receiverRank));
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
    if (!message.text.empty()) {
        writeAll(fd, message.text.data(), message.text.size());
    } else if (!message.words.empty()) {
        writeAll(fd, message.words.data(), message.words.size() * sizeof(int64_t));
    }
    close(fd);
}

void TcpRoutedPeerTransport::finalize() {
    if (_finalized) {
        return;
    }
    _finalized = true;
    try {
        int wakeFd = connectToPort(portForRank(_rank));
        close(wakeFd);
    } catch (...) {
        // Closing the listening fd below is the fallback for a partially initialized transport.
    }
    closeFd(_listenFd);
    if (_listenerThread.joinable()) {
        _listenerThread.join();
    }
}

void TcpRoutedPeerTransport::listenerLoop() {
    while (true) {
        int fd = accept(_listenFd, nullptr, nullptr);
        if (fd < 0) {
            if (_finalized || errno == EBADF) {
                return;
            }
            if (errno == EINTR) {
                continue;
            }
            return;
        }

        int64_t header[5]{};
        if (!readAll(fd, header, sizeof(header))) {
            close(fd);
            continue;
        }

        RoutedPeerMessage message;
        message.senderRank = static_cast<int>(header[0]);
        message.receiverRank = static_cast<int>(header[1]);
        message.tag = static_cast<int>(header[2]);
        message.type = static_cast<int>(header[3]);
        if (header[4] < 0) {
            close(fd);
            continue;
        }
        const auto size = static_cast<size_t>(header[4]);

        if (message.type == kStringMessage) {
            message.text.resize(size);
            if (size > 0 && !readAll(fd, message.text.data(), size)) {
                close(fd);
                continue;
            }
        } else {
            message.words.resize(size);
            if (size > 0 && !readAll(fd, message.words.data(), size * sizeof(int64_t))) {
                close(fd);
                continue;
            }
        }
        close(fd);
        _handler(std::move(message));
    }
}
