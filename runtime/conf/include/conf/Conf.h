#ifndef PILOT_CONF_H
#define PILOT_CONF_H

#include <cstdint>
#include <string>

class Conf {
public:
    enum CommT { MPI, TCP, L2 };
    enum BmtT { BMT_BACKGROUND, BMT_IN_PATH };
    enum SimulationLevelT { SIMULATION_SOFTWARE, SIMULATION_SIMULATOR };

    static void init(int argc, char **argv);

    inline static BmtT BMT_METHOD = BMT_IN_PATH;
    inline static CommT COMM_TYPE = MPI;
    inline static int ENDPOINT_RANK = 0;
    inline static std::string L2_INTERFACE_SERVER0 = "tap0";
    inline static std::string L2_INTERFACE_SERVER1 = "tap1";
    inline static std::string L2_INTERFACE_CLIENT = "tap2";
    inline static bool ENABLE_CLASS_WISE_TIMING = false;
    inline static bool ENABLE_SIMD = false;
    inline static bool DISABLE_MULTI_THREAD = false;
    inline static bool ENABLE_TRANSFER_COMPRESSION = false;

    inline static SimulationLevelT SIMULATION_LEVEL = SIMULATION_SOFTWARE;
    inline static int TCP_SWITCH_PORT = 19000;

    inline static bool ENABLE_IN_PATH_BMT_SWITCH = true;
    inline static int IN_PATH_SWITCH_RANK = 3;
    inline static int IN_PATH_LANE_NUM = 1;
    inline static int IN_PATH_MAX_PARALLELISM = 1;
    inline static int IN_PATH_BMT_BUNDLE_SIZE = 64;
    inline static int IN_PATH_BOOTSTRAP_BMT_COUNT = 64;

    // Optional FPGA-calibrated virtual timing model. Zero-valued cycle fields
    // are disabled and do not delay execution; estimates can be written to CSV.
    inline static int64_t IN_PATH_HW_CLOCK_HZ = 0;
    inline static int64_t IN_PATH_HW_BMT_PAIR_CYCLES = 0;
    inline static int64_t IN_PATH_HW_REQUEST_FIXED_CYCLES = 0;
    inline static int64_t IN_PATH_HW_REQUEST_BLOCK_CYCLES = 0;
    inline static int64_t IN_PATH_HW_PAYLOAD_WORD_CYCLES = 0;
    inline static int64_t IN_PATH_HW_BMT_APPEND_CYCLES = 0;
    inline static int64_t IN_PATH_HW_RESPONSE_FIXED_CYCLES = 0;
    inline static int64_t IN_PATH_HW_RESPONSE_BLOCK_CYCLES = 0;
    inline static int64_t IN_PATH_HW_AXIS_BEAT_CYCLES = 0;
    inline static std::string IN_PATH_TIMING_TRACE;
};

#endif
