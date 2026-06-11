#ifndef PILOT_CONF_H
#define PILOT_CONF_H

class Conf {
public:
    enum CommT { MPI };
    enum BmtT { BMT_BACKGROUND, BMT_IN_PATH };

    static void init(int argc, char **argv);

    inline static BmtT BMT_METHOD = BMT_IN_PATH;
    inline static CommT COMM_TYPE = MPI;
    inline static bool ENABLE_CLASS_WISE_TIMING = false;
    inline static bool ENABLE_SIMD = false;
    inline static bool DISABLE_MULTI_THREAD = false;
    inline static bool ENABLE_TRANSFER_COMPRESSION = false;

    inline static bool ENABLE_IN_PATH_BMT_SWITCH = true;
    inline static int IN_PATH_SWITCH_RANK = 3;
    inline static int IN_PATH_LANE_NUM = 1;
    inline static int IN_PATH_MAX_PARALLELISM = 1;
    inline static int IN_PATH_BMT_BUNDLE_SIZE = 64;
    inline static int IN_PATH_BOOTSTRAP_BMT_COUNT = 64;
};

#endif
