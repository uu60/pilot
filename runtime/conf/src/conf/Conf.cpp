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
    if (value == "routed" || value == "standalone") {
        return Conf::ROUTED;
    }
    throw std::runtime_error("Unsupported --comm. Expected mpi or routed.");
}

int getRoutedRank(int argc, char **argv) {
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
    return getInt(argc, argv, "rank", Conf::ROUTED_RANK);
}

Conf::ServerTransportT getServerTransport(int argc, char **argv) {
    const auto value = getString(argc, argv, "server_transport", "mpi_switch");
    if (value == "mpi_switch" || value == "mpi") {
        return Conf::SERVER_TRANSPORT_MPI;
    }
    if (value == "tcp_switch" || value == "tcp") {
        return Conf::SERVER_TRANSPORT_TCP;
    }
    throw std::runtime_error("Unsupported --server_transport. Expected mpi_switch/mpi or tcp_switch/tcp.");
}

Conf::SimulationLevelT getSimulationLevel(int argc, char **argv) {
    const auto value = getString(argc, argv, "simulation_level", "software");
    if (value == "software") {
        return Conf::SIMULATION_SOFTWARE;
    }
    if (value == "simulator") {
        return Conf::SIMULATION_SIMULATOR;
    }
    throw std::runtime_error("Unsupported --simulation_level. Expected software or simulator.");
}

Conf::RoutedNetworkT getRoutedNetwork(int argc, char **argv) {
    const auto value = getString(argc, argv, "routed_network", "tcp");
    if (value == "tcp") {
        return Conf::ROUTED_NETWORK_TCP;
    }
    if (value == "tap") {
        return Conf::ROUTED_NETWORK_TAP;
    }
    throw std::runtime_error("Unsupported --routed_network. Expected tcp or tap.");
}
}

void Conf::init(int argc, char **argv) {
    COMM_TYPE = getCommType(argc, argv);
    ROUTED_RANK = getRoutedRank(argc, argv);
    ROUTED_BASE_PORT = getInt(argc, argv, "routed_base_port",
                              getInt(argc, argv, "standalone_base_port", ROUTED_BASE_PORT));
    ROUTED_NETWORK = getRoutedNetwork(argc, argv);
    ROUTED_TAP_SERVER0 = getString(argc, argv, "routed_tap_server0", ROUTED_TAP_SERVER0);
    ROUTED_TAP_SERVER1 = getString(argc, argv, "routed_tap_server1", ROUTED_TAP_SERVER1);
    SERVER_TRANSPORT = getServerTransport(argc, argv);
    SIMULATION_LEVEL = getSimulationLevel(argc, argv);
    TCP_SWITCH_PORT = getInt(argc, argv, "tcp_switch_port", TCP_SWITCH_PORT);
    IN_PATH_MAX_PARALLELISM = getInt(argc, argv, "in_path_max_parallelism", IN_PATH_MAX_PARALLELISM);
    IN_PATH_BMT_BUNDLE_SIZE = getInt(argc, argv, "in_path_bmt_bundle_size", IN_PATH_BMT_BUNDLE_SIZE);
    IN_PATH_SWITCH_RANK = getInt(argc, argv, "in_path_switch_rank", IN_PATH_SWITCH_RANK);
    IN_PATH_LANE_NUM = std::max(1, IN_PATH_MAX_PARALLELISM);
    IN_PATH_BOOTSTRAP_BMT_COUNT = IN_PATH_BMT_BUNDLE_SIZE;
    LaneThreadPool::init(IN_PATH_LANE_NUM);
    LaneThreadPool::bindLane(0);
}
