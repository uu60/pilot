#ifndef PILOT_SIMULATOR_SWITCH_TRANSPORT_H
#define PILOT_SIMULATOR_SWITCH_TRANSPORT_H

#include "comm/transport/SwitchFrameTransport.h"

class SimulatorSwitchTransport final : public SwitchFrameTransport {
public:
    void send(const PilotFrame &frame) override;

    PilotFrame receive(int physicalTag) override;

    void finalize() override;

    static void runSwitch();
};

#endif
