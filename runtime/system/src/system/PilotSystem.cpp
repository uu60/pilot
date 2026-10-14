#include "system/PilotSystem.h"

#include "comm/Comm.h"
#include "conf/Conf.h"
#include "comm/switch/MpiSoftwareSwitch.h"
#include "comm/switch/TcpSoftwareSwitch.h"
#include "intermediate/IntermediateDataSupport.h"
#include "parallel/LaneThreadPool.h"

#include <cstdlib>
#include <future>
#include <stdexcept>
#include <vector>

void PilotSystem::init(int argc, char **argv) {
    Conf::init(argc, argv);
    Comm::init(argc, argv);

    if (Comm::rank() == Conf::IN_PATH_SWITCH_RANK) {
        if (Conf::COMM_TYPE == Conf::TCP) {
            TcpSoftwareSwitch::run();
        } else if (Conf::COMM_TYPE == Conf::MPI) {
            MpiSoftwareSwitch::run();
        } else {
            throw std::runtime_error("L2Comm uses an external switch and has no Pilot switch role.");
        }
        Comm::finalize();
        std::exit(0);
    }

    if (Comm::isServer()) {
        IntermediateDataSupport::init();
    }
    bootstrapInPathBmts();
}

void PilotSystem::finalize() {
    if (Comm::isServer()) {
        if (Conf::COMM_TYPE == Conf::MPI) {
            MpiSoftwareSwitch::sendShutdown();
        }
    }
    Comm::finalize();
}

void PilotSystem::bootstrapInPathBmts() {
    if (!Comm::isServer()) {
        return;
    }

    std::vector<std::future<void>> futures;
    futures.reserve(Conf::IN_PATH_LANE_NUM);
    for (int lane = 0; lane < Conf::IN_PATH_LANE_NUM; ++lane) {
        futures.push_back(LaneThreadPool::submit(lane, [lane]() {
            IntermediateDataSupport::requestBitwiseBmts(Conf::IN_PATH_BOOTSTRAP_BMT_COUNT);
            std::vector<int64_t> dummy{0};
            std::vector<int64_t> peer;
            auto sendRequest = Comm::serverSendAsync(dummy, 64);
            auto receiveRequest = Comm::serverReceiveAsync(peer, static_cast<int>(dummy.size()), 64);
            Comm::wait(sendRequest);
            Comm::wait(receiveRequest);
        }));
    }
    for (auto &future: futures) {
        future.get();
    }
}
