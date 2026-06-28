#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {
std::string getValueArg(int argc, char **argv, const std::string &name, const std::string &fallback) {
    const std::string prefix = "--" + name + "=";
    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if (arg.rfind(prefix, 0) == 0) {
            return arg.substr(prefix.size());
        }
    }
    return fallback;
}

bool hasPrefix(const std::string &arg, const std::string &name) {
    return arg.rfind("--" + name + "=", 0) == 0;
}

bool hasArg(int argc, char **argv, const std::string &name) {
    for (int i = 1; i < argc; ++i) {
        if (hasPrefix(argv[i], name)) {
            return true;
        }
    }
    return false;
}

bool isSimulatorMode(int argc, char **argv) {
    return getValueArg(argc, argv, "simulation_level", "software") == "simulator";
}

std::vector<std::string> makeArgs(int argc, char **argv, const std::string &program, const std::string &role) {
    std::vector<std::string> args;
    args.push_back(program);
    args.push_back("--comm=routed");
    args.push_back("--role=" + role);
    if (!hasArg(argc, argv, "server_transport")) {
        args.push_back("--server_transport=tcp");
    }
    if (!hasArg(argc, argv, "simulation_level")) {
        args.push_back("--simulation_level=software");
    }
    if (!hasArg(argc, argv, "routed_network")) {
        args.push_back(isSimulatorMode(argc, argv) ? "--routed_network=tap" : "--routed_network=tcp");
    }

    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if (hasPrefix(arg, "program") || hasPrefix(arg, "comm") || hasPrefix(arg, "role") || hasPrefix(arg, "rank")) {
            continue;
        }
        args.push_back(arg);
    }
    return args;
}

pid_t spawnRole(const std::vector<std::string> &args) {
    pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "fork failed: " << std::strerror(errno) << '\n';
        return -1;
    }
    if (pid == 0) {
        std::vector<char *> execArgs;
        execArgs.reserve(args.size() + 1);
        for (const auto &arg: args) {
            execArgs.push_back(const_cast<char *>(arg.c_str()));
        }
        execArgs.push_back(nullptr);
        execv(execArgs[0], execArgs.data());
        std::cerr << "exec failed for " << execArgs[0] << ": " << std::strerror(errno) << '\n';
        _exit(127);
    }
    return pid;
}
}

int main(int argc, char **argv) {
    const auto program = getValueArg(argc, argv, "program", "build/db/benchmark/pilot_db_sort");
    const std::vector<std::string> roles = isSimulatorMode(argc, argv)
                                               ? std::vector<std::string>{"server0", "server1", "client"}
                                               : std::vector<std::string>{"switch", "server0", "server1", "client"};
    std::vector<pid_t> pids;
    pids.reserve(roles.size());

    for (const auto &role: roles) {
        const auto args = makeArgs(argc, argv, program, role);
        pid_t pid = spawnRole(args);
        if (pid < 0) {
            return EXIT_FAILURE;
        }
        pids.push_back(pid);
        if (role == "switch") {
            usleep(200000);
        }
    }

    int exitCode = 0;
    for (pid_t pid: pids) {
        int status = 0;
        if (waitpid(pid, &status, 0) < 0) {
            std::cerr << "waitpid failed: " << std::strerror(errno) << '\n';
            exitCode = 1;
            continue;
        }
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            exitCode = 1;
        }
    }
    return exitCode;
}
