#ifndef PILOT_CONF_H
#define PILOT_CONF_H

class Conf {
public:
    enum CommT { MPI };
    enum BmtT { BMT_BACKGROUND, BMT_IN_PATH };
    enum ServerTransportT { SERVER_TRANSPORT_MPI, SERVER_TRANSPORT_TCP };
    enum SimulationLevelT { SIMULATION_SOFTWARE, SIMULATION_SIMULATOR };

    static void init(int argc, char **argv);

    inline static BmtT BMT_METHOD = BMT_IN_PATH;
    inline static CommT COMM_TYPE = MPI;
    inline static bool ENABLE_CLASS_WISE_TIMING = false;
    inline static bool ENABLE_SIMD = false;
    inline static bool DISABLE_MULTI_THREAD = false;
    inline static bool ENABLE_TRANSFER_COMPRESSION = false;

    inline static ServerTransportT SERVER_TRANSPORT = SERVER_TRANSPORT_MPI;
    inline static SimulationLevelT SIMULATION_LEVEL = SIMULATION_SOFTWARE;
    inline static int TCP_SWITCH_PORT = 19000;

    inline static bool ENABLE_IN_PATH_BMT_SWITCH = true;
    inline static int IN_PATH_SWITCH_RANK = 3;
    inline static int IN_PATH_LANE_NUM = 1;
    inline static int IN_PATH_MAX_PARALLELISM = 1;
    inline static int IN_PATH_BMT_BUNDLE_SIZE = 64;
    inline static int IN_PATH_BOOTSTRAP_BMT_COUNT = 64;
};

#endif
