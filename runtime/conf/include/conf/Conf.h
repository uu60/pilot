#ifndef PILOT_CONF_H
#define PILOT_CONF_H

#include <string>

class Conf {
public:
    enum CommT { MPI, ROUTED };
    enum BmtT { BMT_BACKGROUND, BMT_IN_PATH };
    enum ServerTransportT { SERVER_TRANSPORT_MPI, SERVER_TRANSPORT_TCP };
    enum SimulationLevelT { SIMULATION_SOFTWARE, SIMULATION_SIMULATOR };
    enum RoutedNetworkT { ROUTED_NETWORK_TCP, ROUTED_NETWORK_TAP };

    static void init(int argc, char **argv);

    inline static BmtT BMT_METHOD = BMT_IN_PATH;
    inline static CommT COMM_TYPE = MPI;
    inline static int ROUTED_RANK = 0;
    inline static int ROUTED_BASE_PORT = 20000;
    inline static RoutedNetworkT ROUTED_NETWORK = ROUTED_NETWORK_TCP;
    inline static std::string ROUTED_TAP_SERVER0 = "tap0";
    inline static std::string ROUTED_TAP_SERVER1 = "tap1";
    inline static std::string ROUTED_TAP_CLIENT = "tap2";
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
