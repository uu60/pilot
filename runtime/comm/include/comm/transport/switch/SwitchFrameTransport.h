#ifndef PILOT_SWITCH_FRAME_TRANSPORT_H
#define PILOT_SWITCH_FRAME_TRANSPORT_H

#include "comm/protocol/PilotFrame.h"

class SwitchFrameTransport {
public:
    virtual ~SwitchFrameTransport() = default;

    virtual void send(const PilotFrame &frame) = 0;

    virtual PilotFrame receive(int physicalTag) = 0;

    virtual void finalize() = 0;
};

#endif
