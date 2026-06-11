#include "basis/View.h"
#include "comm/Comm.h"
#include "compute/BitwiseOperator.h"
#include "system/PilotSystem.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr int kWidth = 16;
constexpr int kShareTag = 10;
constexpr int kRevealTag = 20;

int getIntArg(int argc, char **argv, const std::string &name, int fallback) {
    const std::string prefix = "--" + name + "=";
    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if (arg.rfind(prefix, 0) == 0) {
            return std::stoi(arg.substr(prefix.size()));
        }
    }
    return fallback;
}

bool isPowerOfTwo(int value) {
    return value > 0 && (value & (value - 1)) == 0;
}

std::vector<int64_t> makeInput(int rows) {
    std::vector<int64_t> values;
    values.reserve(rows);
    for (int i = 0; i < rows; ++i) {
        values.push_back((rows * 17 - i * 11 + (i % 3) * 7) & ((1 << kWidth) - 1));
    }
    return values;
}
}

int main(int argc, char **argv) {
    PilotSystem::init(argc, argv);

    const int rows = getIntArg(argc, argv, "rows", 8);
    if (!isPowerOfTwo(rows)) {
        throw std::runtime_error("pilot_db_sort requires --rows to be a power of two.");
    }

    std::vector<int64_t> input;
    if (Comm::isClient()) {
        input = makeInput(rows);
    }

    BitwiseOperator shared(input, kWidth, kShareTag, Comm::CLIENT_RANK);
    std::vector<int64_t> shares = std::move(shared._zis);

    if (Comm::isServer()) {
        std::vector<std::string> fieldNames{"key"};
        std::vector<int> fieldWidths{kWidth};
        View view(fieldNames, fieldWidths);
        view._dataCols[0] = shares;
        view._dataCols[view.colNum() + View::VALID_COL_OFFSET].assign(shares.size(), Comm::rank());
        view._dataCols[view.colNum() + View::PADDING_COL_OFFSET].assign(shares.size(), 0);
        view.sort("key", true);
        shares = std::move(view._dataCols[0]);
    }

    BitwiseOperator revealed(shares, kWidth, kRevealTag, SecureOperator::NO_CLIENT_COMPUTE);
    revealed.reconstruct(Comm::CLIENT_RANK);

    if (Comm::isClient()) {
        auto expected = input;
        std::sort(expected.begin(), expected.end());
        const bool ok = revealed._results == expected;
        std::cout << "pilot_db_sort " << (ok ? "PASS" : "FAIL") << "\n";
        std::cout << "input:";
        for (auto v: input) std::cout << ' ' << v;
        std::cout << "\noutput:";
        for (auto v: revealed._results) std::cout << ' ' << v;
        std::cout << "\n";
    }

    PilotSystem::finalize();
    return 0;
}
