#include "basis/View.h"
#include "comm/Comm.h"
#include "compute/BitwiseOperator.h"
#include "system/PilotSystem.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
constexpr int kWidth = 16;
constexpr int kShareTag = 10;
constexpr int kRevealTag = 20;
constexpr int kShareTagSecond = 11;
constexpr int kRevealTagSecond = 21;

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

std::vector<int64_t> makeSecondaryInput(const std::vector<int64_t> &primary) {
    std::vector<int64_t> values;
    values.reserve(primary.size());
    for (int i = 0; i < static_cast<int>(primary.size()); ++i) {
        values.push_back((static_cast<int64_t>(i * 19 + 5) ^ primary[i]) & ((1 << kWidth) - 1));
    }
    return values;
}
}

int main(int argc, char **argv) {
    PilotSystem::init(argc, argv);

    const int rows = getIntArg(argc, argv, "rows", 8);
    const bool multiCols = getIntArg(argc, argv, "multi_cols", 0) != 0;
    if (!isPowerOfTwo(rows)) {
        throw std::runtime_error("pilot_db_sort requires --rows to be a power of two.");
    }

    std::vector<int64_t> input;
    std::vector<int64_t> secondaryInput;
    if (Comm::isClient()) {
        input = makeInput(rows);
        if (multiCols) {
            for (int i = 0; i < rows; ++i) {
                input[i] = (rows - 1 - i) / 2;
            }
            secondaryInput = makeSecondaryInput(input);
        }
    }

    BitwiseOperator shared(input, kWidth, kShareTag, Comm::CLIENT_RANK);
    std::vector<int64_t> shares = std::move(shared._zis);
    std::vector<int64_t> secondaryShares;
    if (multiCols) {
        BitwiseOperator sharedSecond(secondaryInput, kWidth, kShareTagSecond, Comm::CLIENT_RANK);
        secondaryShares = std::move(sharedSecond._zis);
    }

    if (Comm::isServer()) {
        std::vector<std::string> fieldNames = multiCols ? std::vector<std::string>{"key0", "key1"}
                                                        : std::vector<std::string>{"key"};
        std::vector<int> fieldWidths(fieldNames.size(), kWidth);
        View view(fieldNames, fieldWidths);
        view._dataCols[0] = shares;
        if (multiCols) {
            view._dataCols[1] = secondaryShares;
        }
        view._dataCols[view.colNum() + View::VALID_COL_OFFSET].assign(shares.size(), Comm::rank());
        view._dataCols[view.colNum() + View::PADDING_COL_OFFSET].assign(shares.size(), 0);
        if (multiCols) {
            std::vector<std::string> orderFields{"key0", "key1"};
            std::vector<bool> ascendingOrders{true, true};
            view.sort(orderFields, ascendingOrders);
        } else {
            view.sort("key", true);
        }
        shares = std::move(view._dataCols[0]);
        if (multiCols) {
            secondaryShares = std::move(view._dataCols[1]);
        }
    }

    BitwiseOperator revealed(shares, kWidth, kRevealTag, SecureOperator::NO_CLIENT_COMPUTE);
    revealed.reconstruct(Comm::CLIENT_RANK);
    BitwiseOperator revealedSecond(secondaryShares, kWidth, kRevealTagSecond, SecureOperator::NO_CLIENT_COMPUTE);
    if (multiCols) {
        revealedSecond.reconstruct(Comm::CLIENT_RANK);
    }

    if (Comm::isClient()) {
        bool ok = false;
        if (multiCols) {
            std::vector<std::pair<int64_t, int64_t>> expected;
            expected.reserve(input.size());
            for (size_t i = 0; i < input.size(); ++i) {
                expected.emplace_back(input[i], secondaryInput[i]);
            }
            std::sort(expected.begin(), expected.end());
            ok = revealed._results.size() == expected.size() && revealedSecond._results.size() == expected.size();
            for (size_t i = 0; ok && i < expected.size(); ++i) {
                ok = revealed._results[i] == expected[i].first && revealedSecond._results[i] == expected[i].second;
            }
        } else {
            auto expected = input;
            std::sort(expected.begin(), expected.end());
            ok = revealed._results == expected;
        }
        std::cout << "pilot_db_sort " << (ok ? "PASS" : "FAIL") << "\n";
        std::cout << "input:";
        for (auto v: input) std::cout << ' ' << v;
        std::cout << "\noutput:";
        for (auto v: revealed._results) std::cout << ' ' << v;
        std::cout << "\n";
        if (multiCols) {
            std::cout << "secondary_input:";
            for (auto v: secondaryInput) std::cout << ' ' << v;
            std::cout << "\nsecondary_output:";
            for (auto v: revealedSecond._results) std::cout << ' ' << v;
            std::cout << "\n";
        }
    }

    PilotSystem::finalize();
    return 0;
}
