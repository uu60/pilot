#include "comm/TcpComm.h"

#include "comm/Comm.h"
#include "comm/InPathSwitchSimulator.h"
#include "comm/item/FutureRequestWrapper.h"
#include "conf/Conf.h"
#include "intermediate/IntermediateDataSupport.h"
#include "utils/Log.h"

#include <arpa/inet.h>
#include <atomic>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <future>
#include <netinet/in.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {
constexpr int kListenBacklog = 2;
constexpr int64_t kHelloTag = -1;

int inPathPhysicalTag() {
    return Comm::TAG_SWITCH_LANE_BASE + IntermediateDataSupport::currentLane();
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
        const ssize_t written = send(fd, ptr, bytes, 0);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            throwSystemError("socket send failed");
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
            throwSystemError("socket receive failed");
        }
        ptr += readBytes;
        bytes -= static_cast<size_t>(readBytes);
    }
    return true;
}

int createClientSocket() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        throwSystemError("socket creation failed");
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<uint16_t>(Conf::TCP_SWITCH_PORT));
    if (inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) != 1) {
        close(fd);
        throw std::runtime_error("Invalid TCP switch host.");
    }

    for (int attempt = 0; attempt < 200; ++attempt) {
        if (connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0) {
            return fd;
        }
        if (errno != ECONNREFUSED && errno != ENOENT) {
            close(fd);
            throwSystemError("socket connect failed");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    close(fd);
    throw std::runtime_error("Timed out connecting to TCP switch transport.");
}

int createServerSocket() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        throwSystemError("switch socket creation failed");
    }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(static_cast<uint16_t>(Conf::TCP_SWITCH_PORT));
    if (bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) {
        close(fd);
        throwSystemError("switch socket bind failed");
    }
    if (listen(fd, kListenBacklog) != 0) {
        close(fd);
        throwSystemError("switch socket listen failed");
    }
    return fd;
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

    Log::i("TCP in-path BMT software switch started.");
    int listenFd = createServerSocket();
    std::array<int, 2> serverFds{-1, -1};

    while (serverFds[0] < 0 || serverFds[1] < 0) {
        int fd = accept(listenFd, nullptr, nullptr);
        if (fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            closeFd(listenFd);
            throwSystemError("switch socket accept failed");
        }

        PilotFrame hello;
        if (!receiveFrame(fd, hello) || hello.physicalTag != kHelloTag) {
            close(fd);
            continue;
        }
        if (!Comm::isServerRank(hello.sourceRank)) {
            close(fd);
            continue;
        }
        serverFds[static_cast<size_t>(hello.sourceRank)] = fd;
    }

    std::atomic_int shutdowns{0};
    auto worker = [&](int srcRank) {
        const int dstRank = srcRank == Comm::SERVER0_RANK ? Comm::SERVER1_RANK : Comm::SERVER0_RANK;
        while (shutdowns.load() < 2) {
            PilotFrame request;
            if (!receiveFrame(serverFds[static_cast<size_t>(srcRank)], request)) {
                break;
            }
            if (request.physicalTag == InPathSwitchSimulator::CONTROL_TAG &&
                request.words.size() == 1 &&
                request.words[0] == InPathSwitchSimulator::SHUTDOWN_MAGIC) {
                ++shutdowns;
                break;
            }
            auto envelope = InPathSwitchSimulator::forwardRequest(srcRank, request.physicalTag, request.words);
            sendFrame(serverFds[static_cast<size_t>(dstRank)],
                      PilotFrame{Conf::IN_PATH_SWITCH_RANK, dstRank, request.physicalTag, std::move(envelope)});
        }
    };

    std::thread fromServer0(worker, Comm::SERVER0_RANK);
    std::thread fromServer1(worker, Comm::SERVER1_RANK);
    fromServer0.join();
    fromServer1.join();

    closeFd(serverFds[0]);
    closeFd(serverFds[1]);
    closeFd(listenFd);
    Log::i("TCP in-path BMT software switch stopped.");
}

void TcpComm::sendShutdown() {
    if (!enabled() || !Comm::isServerRank(Comm::rank())) {
        return;
    }
    ensureClientConnected();
    std::vector<int64_t> stop{InPathSwitchSimulator::SHUTDOWN_MAGIC};
    std::lock_guard<std::mutex> lock(_mutex);
    sendFrame(_socketFd,
              PilotFrame{Comm::rank(), Conf::IN_PATH_SWITCH_RANK, InPathSwitchSimulator::CONTROL_TAG, stop});
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
    finalizeTcp();
    MpiComm::finalize_();
}

void TcpComm::send(int physicalTag, const std::vector<int64_t> &request) {
    ensureClientConnected();
    std::lock_guard<std::mutex> lock(_mutex);
    sendFrame(_socketFd, PilotFrame{Comm::rank(), Conf::IN_PATH_SWITCH_RANK, physicalTag, request});
}

std::vector<int64_t> TcpComm::receive(int physicalTag) {
    ensureClientConnected();
    std::unique_lock<std::mutex> lock(_mutex);
    _cv.wait(lock, [&] { return !_pending[physicalTag].empty(); });
    auto words = std::move(_pending[physicalTag].front());
    _pending[physicalTag].pop_front();
    return words;
}

void TcpComm::ensureClientConnected() {
    if (!enabled() || !Comm::isServerRank(Comm::rank())) {
        return;
    }

    std::lock_guard<std::mutex> lock(_mutex);
    if (_connected) {
        return;
    }
    if (Conf::SIMULATION_LEVEL == Conf::SIMULATION_SIMULATOR) {
        throw std::runtime_error("TCP simulator-level switch client is not connected yet.");
    }
    _socketFd = createClientSocket();
    _connected = true;
    sendFrame(_socketFd, PilotFrame{Comm::rank(), Conf::IN_PATH_SWITCH_RANK, static_cast<int>(kHelloTag), {}});
    _receiveThread = std::thread(receiveLoop);
}

void TcpComm::receiveLoop() {
    try {
        while (true) {
            PilotFrame frame;
            if (!receiveFrame(_socketFd, frame)) {
                break;
            }
            {
                std::lock_guard<std::mutex> lock(_mutex);
                _pending[frame.physicalTag].push_back(std::move(frame.words));
            }
            _cv.notify_all();
        }
    } catch (...) {
        // The socket is closed from finalizeTcp() to unblock the receiver thread.
        _cv.notify_all();
    }
}

void TcpComm::finalizeTcp() {
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_finalized) {
            return;
        }
        _finalized = true;
        closeFd(_socketFd);
    }
    if (_receiveThread.joinable()) {
        _receiveThread.join();
    }
}

void TcpComm::sendFrame(int fd, const PilotFrame &frame) {
    const int64_t header[4]{
        static_cast<int64_t>(frame.sourceRank),
        static_cast<int64_t>(frame.destinationRank),
        static_cast<int64_t>(frame.physicalTag),
        static_cast<int64_t>(frame.words.size())
    };
    writeAll(fd, header, sizeof(header));
    if (!frame.words.empty()) {
        writeAll(fd, frame.words.data(), frame.words.size() * sizeof(int64_t));
    }
}

bool TcpComm::receiveFrame(int fd, PilotFrame &frame) {
    int64_t header[4]{};
    if (!readAll(fd, header, sizeof(header))) {
        return false;
    }
    if (header[3] < 0) {
        throw std::runtime_error("Invalid TCP switch frame size.");
    }
    frame.sourceRank = static_cast<int>(header[0]);
    frame.destinationRank = static_cast<int>(header[1]);
    frame.physicalTag = static_cast<int>(header[2]);
    frame.words.resize(static_cast<size_t>(header[3]));
    if (!frame.words.empty() && !readAll(fd, frame.words.data(), frame.words.size() * sizeof(int64_t))) {
        return false;
    }
    return true;
}
