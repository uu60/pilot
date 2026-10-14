#include "comm/switch/MpiSoftwareSwitch.h"

#include "comm/Comm.h"
#include "comm/protocol/InPathBmtProtocol.h"
#include "comm/switch/SoftwareBmtEngine.h"
#include "conf/Conf.h"
#include "utils/Log.h"

#include <cstdint>
#include <mpi.h>
#include <stdexcept>
#include <vector>

bool MpiSoftwareSwitch::isSwitchRank() {
    return InPathBmtProtocol::enabled() && Comm::rank() == Conf::IN_PATH_SWITCH_RANK;
}

void MpiSoftwareSwitch::sendShutdown() {
    if (!InPathBmtProtocol::shouldRouteServerTraffic()) {
        return;
    }
    std::vector<int64_t> stop{SHUTDOWN_MAGIC};
    MPI_Send(stop.data(), static_cast<int>(stop.size()), MPI_INT64_T,
             Conf::IN_PATH_SWITCH_RANK, CONTROL_TAG, MPI_COMM_WORLD);
}

void MpiSoftwareSwitch::run() {
    if (!isSwitchRank()) {
        return;
    }
    if (Conf::COMM_TYPE != Conf::MPI) {
        throw std::runtime_error("MPI software switch requires MPI communication.");
    }

    Log::i("MPI in-path BMT software switch started.");
    int shutdowns = 0;
    while (shutdowns < 2) {
        MPI_Status status{};
        MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
        int count = 0;
        MPI_Get_count(&status, MPI_INT64_T, &count);
        std::vector<int64_t> request(count);
        MPI_Recv(request.data(), count, MPI_INT64_T, status.MPI_SOURCE, status.MPI_TAG,
                 MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        if (status.MPI_TAG == CONTROL_TAG && request.size() == 1 && request[0] == SHUTDOWN_MAGIC) {
            ++shutdowns;
            continue;
        }

        const int dstRank = status.MPI_SOURCE == Comm::SERVER0_RANK
                                ? Comm::SERVER1_RANK : Comm::SERVER0_RANK;
        auto envelope = SoftwareBmtEngine::processRequest(status.MPI_SOURCE, status.MPI_TAG, request);
        MPI_Send(envelope.data(), static_cast<int>(envelope.size()), MPI_INT64_T,
                 dstRank, status.MPI_TAG, MPI_COMM_WORLD);
    }
    Log::i("MPI in-path BMT software switch stopped.");
}
