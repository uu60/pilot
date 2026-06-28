#include "comm/transport/MpiSoftwareSwitchTransport.h"

#include "comm/Comm.h"
#include "comm/InPathSwitchSimulator.h"
#include "conf/Conf.h"

#include <mpi.h>

void MpiSoftwareSwitchTransport::send(const PilotFrame &frame) {
    MPI_Send(frame.words.data(), static_cast<int>(frame.words.size()), MPI_INT64_T,
             Conf::IN_PATH_SWITCH_RANK, frame.physicalTag, MPI_COMM_WORLD);
}

PilotFrame MpiSoftwareSwitchTransport::receive(int physicalTag) {
    MPI_Status status{};
    MPI_Probe(Conf::IN_PATH_SWITCH_RANK, physicalTag, MPI_COMM_WORLD, &status);

    int count = 0;
    MPI_Get_count(&status, MPI_INT64_T, &count);
    PilotFrame frame;
    frame.sourceRank = Conf::IN_PATH_SWITCH_RANK;
    frame.destinationRank = Comm::rank();
    frame.physicalTag = physicalTag;
    frame.words.resize(static_cast<size_t>(count));
    MPI_Recv(frame.words.data(), count, MPI_INT64_T, Conf::IN_PATH_SWITCH_RANK,
             physicalTag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    return frame;
}

void MpiSoftwareSwitchTransport::finalize() {
}

void MpiSoftwareSwitchTransport::runSwitch() {
    InPathSwitchSimulator::run();
}
