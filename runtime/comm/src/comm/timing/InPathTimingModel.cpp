#include "comm/timing/InPathTimingModel.h"

#include "conf/Conf.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>

namespace {
std::mutex g_traceMutex;
std::atomic_uint64_t g_requestSequence{0};

int64_t ceilDiv(int64_t value, int64_t divisor) {
    return divisor == 0 ? 0 : (value + divisor - 1) / divisor;
}
}

void InPathTimingModel::recordRequest(int laneId,
                                      int tag,
                                      int destinationRank,
                                      int bmtCount,
                                      size_t payloadWords,
                                      size_t encryptedTrailerBytes) {
    if (Conf::IN_PATH_TIMING_TRACE.empty()) return;

    constexpr int64_t kEthernetAndPilotHeaderBytes = 46;
    constexpr int64_t kRequestSubheaderBytes = 40;
    constexpr int64_t kEnvelopeSubheaderBytes = 56;
    constexpr int64_t kAuthenticationTagBytes = 16;
    constexpr int64_t kAxiBytesPerBeat = 64;

    const auto requestBytes = kEthernetAndPilotHeaderBytes + kRequestSubheaderBytes +
                              static_cast<int64_t>(payloadWords * 8) + kAuthenticationTagBytes;
    const auto responseBytes = kEthernetAndPilotHeaderBytes + kEnvelopeSubheaderBytes +
                               static_cast<int64_t>(payloadWords * 8 + encryptedTrailerBytes) +
                               kAuthenticationTagBytes;
    const auto requestBlocks = ceilDiv(requestBytes - 14, 64);
    const auto responseBlocks = ceilDiv(responseBytes - 14, 64);
    const auto axisBeats = ceilDiv(responseBytes, kAxiBytesPerBeat);

    const int64_t pregenCycles = static_cast<int64_t>(bmtCount) * Conf::IN_PATH_HW_BMT_PAIR_CYCLES;
    const int64_t requestCycles = Conf::IN_PATH_HW_REQUEST_FIXED_CYCLES +
                                  requestBlocks * Conf::IN_PATH_HW_REQUEST_BLOCK_CYCLES;
    const int64_t responseCycles = Conf::IN_PATH_HW_RESPONSE_FIXED_CYCLES +
                                   static_cast<int64_t>(payloadWords) * Conf::IN_PATH_HW_PAYLOAD_WORD_CYCLES +
                                   static_cast<int64_t>(bmtCount) * Conf::IN_PATH_HW_BMT_APPEND_CYCLES +
                                   responseBlocks * Conf::IN_PATH_HW_RESPONSE_BLOCK_CYCLES +
                                   axisBeats * Conf::IN_PATH_HW_AXIS_BEAT_CYCLES;
    const int64_t serviceCycles = requestCycles + responseCycles;
    const double serviceNanoseconds = Conf::IN_PATH_HW_CLOCK_HZ > 0
                                          ? static_cast<double>(serviceCycles) * 1.0e9 /
                                                static_cast<double>(Conf::IN_PATH_HW_CLOCK_HZ)
                                          : 0.0;

    std::lock_guard<std::mutex> lock(g_traceMutex);
    const bool writeHeader = !std::filesystem::exists(Conf::IN_PATH_TIMING_TRACE) ||
                             std::filesystem::file_size(Conf::IN_PATH_TIMING_TRACE) == 0;
    std::ofstream trace(Conf::IN_PATH_TIMING_TRACE, std::ios::app);
    if (!trace) throw std::runtime_error("Unable to open the in-path timing trace file.");
    if (writeHeader) {
        trace << "request_id,lane,tag,destination,bmt_count,payload_words,trailer_bytes,"
                 "request_bytes,response_bytes,pregen_cycles,service_cycles,service_ns\n";
    }
    trace << g_requestSequence.fetch_add(1) << ',' << laneId << ',' << tag << ',' << destinationRank << ','
          << bmtCount << ',' << payloadWords << ',' << encryptedTrailerBytes << ',' << requestBytes << ','
          << responseBytes << ',' << pregenCycles << ',' << serviceCycles << ',' << serviceNanoseconds << '\n';
}
