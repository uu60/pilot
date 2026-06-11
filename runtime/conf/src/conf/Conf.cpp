#include "conf/Conf.h"

#include "parallel/LaneThreadPool.h"

#include <algorithm>
#include <string>

namespace {
int getInt(int argc, char **argv, const std::string &name, int fallback) {
    const std::string prefix = "--" + name + "=";
    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if (arg.rfind(prefix, 0) == 0) {
            return std::stoi(arg.substr(prefix.size()));
        }
    }
    return fallback;
}
}

void Conf::init(int argc, char **argv) {
    IN_PATH_MAX_PARALLELISM = getInt(argc, argv, "in_path_max_parallelism", IN_PATH_MAX_PARALLELISM);
    IN_PATH_BMT_BUNDLE_SIZE = getInt(argc, argv, "in_path_bmt_bundle_size", IN_PATH_BMT_BUNDLE_SIZE);
    IN_PATH_SWITCH_RANK = getInt(argc, argv, "in_path_switch_rank", IN_PATH_SWITCH_RANK);
    IN_PATH_LANE_NUM = std::max(1, IN_PATH_MAX_PARALLELISM);
    IN_PATH_BOOTSTRAP_BMT_COUNT = IN_PATH_BMT_BUNDLE_SIZE;
    LaneThreadPool::init(IN_PATH_LANE_NUM);
    LaneThreadPool::bindLane(0);
}
