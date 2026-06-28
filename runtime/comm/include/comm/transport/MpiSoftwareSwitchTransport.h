#ifndef PILOT_MPI_SOFTWARE_SWITCH_TRANSPORT_H
#define PILOT_MPI_SOFTWARE_SWITCH_TRANSPORT_H

#include "comm/transport/SwitchFrameTransport.h"

class MpiSoftwareSwitchTransport final : public SwitchFrameTransport {
public:
    void send(const PilotFrame &frame) override;

    PilotFrame receive(int physicalTag) override;

    void finalize() override;

    static void runSwitch();
};

#endif
