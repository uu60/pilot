#include "conf/Conf.h"

#include "parallel/LaneThreadPool.h"

#include <algorithm>
#include <stdexcept>
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

int64_t getInt64(int argc, char **argv, const std::string &name, int64_t fallback) {
    const std::string prefix = "--" + name + "=";
    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if (arg.rfind(prefix, 0) == 0) {
            return std::stoll(arg.substr(prefix.size()));
        }
    }
    return fallback;
}

std::string getString(int argc, char **argv, const std::string &name, const std::string &fallback) {
    const std::string prefix = "--" + name + "=";
    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if (arg.rfind(prefix, 0) == 0) {
            return arg.substr(prefix.size());
        }
    }
    return fallback;
}

Conf::CommT getCommType(int argc, char **argv) {
    const auto value = getString(argc, argv, "comm", "mpi");
    if (value == "mpi") {
        return Conf::MPI;
    }
    if (value == "tcp") {
        return Conf::TCP;
    }
    if (value == "l2" || value == "ethernet") {
        return Conf::L2;
    }
    if (value == "routed" || value == "standalone") {
        const auto network = getString(argc, argv, "routed_network", "tcp");
        return network == "tap" || network == "l2" || network == "ethernet"
                   ? Conf::L2 : Conf::TCP;
    }
    throw std::runtime_error("Unsupported --comm. Expected mpi, tcp, or l2.");
}

int getEndpointRank(int argc, char **argv) {
    const auto role = getString(argc, argv, "role", "");
    if (role == "server0") {
        return 0;
    }
    if (role == "server1") {
        return 1;
    }
    if (role == "client") {
        return 2;
    }
    if (role == "switch") {
        return 3;
    }
    return getInt(argc, argv, "rank", Conf::ENDPOINT_RANK);
}

Conf::SimulationLevelT getSimulationLevel(int argc, char **argv, Conf::SimulationLevelT fallback) {
    const auto value = getString(argc, argv, "simulation_level",
                                 fallback == Conf::SIMULATION_SIMULATOR ? "simulator" : "software");
    if (value == "software") {
        return Conf::SIMULATION_SOFTWARE;
    }
    if (value == "simulator") {
        return Conf::SIMULATION_SIMULATOR;
    }
    throw std::runtime_error("Unsupported --simulation_level. Expected software or simulator.");
}

}

void Conf::init(int argc, char **argv) {
    COMM_TYPE = getCommType(argc, argv);
    ENDPOINT_RANK = getEndpointRank(argc, argv);
    L2_INTERFACE_SERVER0 = getString(argc, argv, "l2_server0",
        getString(argc, argv, "routed_tap_server0", L2_INTERFACE_SERVER0));
    L2_INTERFACE_SERVER1 = getString(argc, argv, "l2_server1",
        getString(argc, argv, "routed_tap_server1", L2_INTERFACE_SERVER1));
    L2_INTERFACE_CLIENT = getString(argc, argv, "l2_client",
        getString(argc, argv, "routed_tap_client", L2_INTERFACE_CLIENT));
    SIMULATION_LEVEL = getSimulationLevel(argc, argv,
        COMM_TYPE == L2 ? SIMULATION_SIMULATOR : SIMULATION_SOFTWARE);
    TCP_SWITCH_PORT = getInt(argc, argv, "tcp_switch_port", TCP_SWITCH_PORT);
    IN_PATH_MAX_PARALLELISM = getInt(argc, argv, "in_path_max_parallelism", IN_PATH_MAX_PARALLELISM);
    IN_PATH_BMT_BUNDLE_SIZE = getInt(argc, argv, "in_path_bmt_bundle_size", IN_PATH_BMT_BUNDLE_SIZE);
    IN_PATH_SWITCH_RANK = getInt(argc, argv, "in_path_switch_rank", IN_PATH_SWITCH_RANK);
    IN_PATH_HW_CLOCK_HZ = getInt64(argc, argv, "in_path_hw_clock_hz", IN_PATH_HW_CLOCK_HZ);
    IN_PATH_HW_BMT_PAIR_CYCLES = getInt64(argc, argv, "in_path_hw_bmt_pair_cycles", IN_PATH_HW_BMT_PAIR_CYCLES);
    IN_PATH_HW_REQUEST_FIXED_CYCLES = getInt64(argc, argv, "in_path_hw_request_fixed_cycles", IN_PATH_HW_REQUEST_FIXED_CYCLES);
    IN_PATH_HW_REQUEST_BLOCK_CYCLES = getInt64(argc, argv, "in_path_hw_request_block_cycles", IN_PATH_HW_REQUEST_BLOCK_CYCLES);
    IN_PATH_HW_PAYLOAD_WORD_CYCLES = getInt64(argc, argv, "in_path_hw_payload_word_cycles", IN_PATH_HW_PAYLOAD_WORD_CYCLES);
    IN_PATH_HW_BMT_APPEND_CYCLES = getInt64(argc, argv, "in_path_hw_bmt_append_cycles", IN_PATH_HW_BMT_APPEND_CYCLES);
    IN_PATH_HW_RESPONSE_FIXED_CYCLES = getInt64(argc, argv, "in_path_hw_response_fixed_cycles", IN_PATH_HW_RESPONSE_FIXED_CYCLES);
    IN_PATH_HW_RESPONSE_BLOCK_CYCLES = getInt64(argc, argv, "in_path_hw_response_block_cycles", IN_PATH_HW_RESPONSE_BLOCK_CYCLES);
    IN_PATH_HW_AXIS_BEAT_CYCLES = getInt64(argc, argv, "in_path_hw_axis_beat_cycles", IN_PATH_HW_AXIS_BEAT_CYCLES);
    IN_PATH_TIMING_TRACE = getString(argc, argv, "in_path_timing_trace", IN_PATH_TIMING_TRACE);
    if (IN_PATH_HW_CLOCK_HZ < 0 || IN_PATH_HW_BMT_PAIR_CYCLES < 0 ||
        IN_PATH_HW_REQUEST_FIXED_CYCLES < 0 || IN_PATH_HW_REQUEST_BLOCK_CYCLES < 0 ||
        IN_PATH_HW_PAYLOAD_WORD_CYCLES < 0 || IN_PATH_HW_BMT_APPEND_CYCLES < 0 ||
        IN_PATH_HW_RESPONSE_FIXED_CYCLES < 0 || IN_PATH_HW_RESPONSE_BLOCK_CYCLES < 0 ||
        IN_PATH_HW_AXIS_BEAT_CYCLES < 0) {
        throw std::runtime_error("In-path hardware timing values must be non-negative.");
    }
    IN_PATH_LANE_NUM = std::max(1, IN_PATH_MAX_PARALLELISM);
    IN_PATH_BOOTSTRAP_BMT_COUNT = IN_PATH_BMT_BUNDLE_SIZE;
    LaneThreadPool::init(IN_PATH_LANE_NUM);
    LaneThreadPool::bindLane(0);
}
