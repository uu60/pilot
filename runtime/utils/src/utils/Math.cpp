#include "utils/Math.h"

#include <random>

thread_local std::mt19937_64 generator(std::random_device{}());

int64_t Math::randInt() {
    return randInt(0, INT64_MAX);
}

int64_t Math::randInt(int64_t lowest, int64_t highest) {
    std::uniform_int_distribution<int64_t> dist(lowest, highest);
    return dist(generator);
}
