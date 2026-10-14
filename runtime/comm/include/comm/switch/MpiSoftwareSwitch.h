#ifndef PILOT_MPI_SOFTWARE_SWITCH_H
#define PILOT_MPI_SOFTWARE_SWITCH_H

class MpiSoftwareSwitch {
public:
    static constexpr int CONTROL_TAG = 0x3ffffffe;
    static constexpr long long SHUTDOWN_MAGIC = 0x5357485f53544f50LL;

    static bool isSwitchRank();

    static void run();

    static void sendShutdown();
};

#endif
