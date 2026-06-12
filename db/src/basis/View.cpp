#include "basis/View.h"

#include "comm/Comm.h"
#include "compute/BitwiseLessOperator.h"
#include "compute/BitwiseMutexOperator.h"
#include "conf/Conf.h"
#include "parallel/LaneThreadPool.h"

#include <algorithm>
#include <cmath>
#include <future>
#include <stdexcept>
#include <utility>

View::View(std::vector<std::string> fieldNames, std::vector<int> fieldWidths)
    : Table(EMPTY_VIEW_NAME, std::move(fieldNames), std::move(fieldWidths), EMPTY_KEY_FIELD) {
    addRedundantCols();
}

View::View(std::string tableName, std::vector<std::string> fieldNames, std::vector<int> fieldWidths)
    : Table(std::move(tableName), std::move(fieldNames), std::move(fieldWidths), EMPTY_KEY_FIELD) {
    addRedundantCols();
}

View::View(std::string tableName, std::vector<std::string> fieldNames, std::vector<int> fieldWidths, bool skipRedundant)
    : Table(std::move(tableName), std::move(fieldNames), std::move(fieldWidths), EMPTY_KEY_FIELD) {
    if (!skipRedundant) {
        addRedundantCols();
    }
}

void View::addRedundantCols() {
    if (colIndex(VALID_COL_NAME) >= 0 || colIndex(PADDING_COL_NAME) >= 0) {
        return;
    }
    const size_t rows = rowNum();
    _fieldNames.push_back(VALID_COL_NAME);
    _fieldWidths.push_back(1);
    _dataCols.emplace_back(rows, Comm::rank());

    _fieldNames.push_back(PADDING_COL_NAME);
    _fieldWidths.push_back(1);
    _dataCols.emplace_back(rows, 0);
}

void View::select(const std::vector<std::string> &fieldNames) {
    if (fieldNames.empty()) {
        return;
    }

    std::vector<int> selectedIndices;
    selectedIndices.reserve(fieldNames.size());
    for (const auto &fieldName: fieldNames) {
        const int index = colIndex(fieldName);
        if (index >= 0) {
            selectedIndices.push_back(index);
        }
    }
    if (selectedIndices.empty()) {
        return;
    }

    const int validColIndex = static_cast<int>(colNum()) + VALID_COL_OFFSET;
    const int paddingColIndex = static_cast<int>(colNum()) + PADDING_COL_OFFSET;

    std::vector<std::vector<int64_t>> newDataCols;
    std::vector<std::string> newFieldNames;
    std::vector<int> newFieldWidths;
    newDataCols.reserve(selectedIndices.size() + 2);
    newFieldNames.reserve(selectedIndices.size() + 2);
    newFieldWidths.reserve(selectedIndices.size() + 2);

    for (int index: selectedIndices) {
        newDataCols.push_back(std::move(_dataCols[index]));
        newFieldNames.push_back(std::move(_fieldNames[index]));
        newFieldWidths.push_back(_fieldWidths[index]);
    }

    newDataCols.push_back(std::move(_dataCols[validColIndex]));
    newFieldNames.push_back(VALID_COL_NAME);
    newFieldWidths.push_back(1);
    newDataCols.push_back(std::move(_dataCols[paddingColIndex]));
    newFieldNames.push_back(PADDING_COL_NAME);
    newFieldWidths.push_back(1);

    _dataCols = std::move(newDataCols);
    _fieldNames = std::move(newFieldNames);
    _fieldWidths = std::move(newFieldWidths);
}

void View::sort(const std::string &orderField, bool ascendingOrder) {
    std::vector<std::string> fields{orderField};
    std::vector<bool> orders{ascendingOrder};
    sort(fields, orders);
}

void View::sort(const std::vector<std::string> &orderFields, const std::vector<bool> &ascendingOrders) {
    if (orderFields.empty() || orderFields.size() != ascendingOrders.size() || rowNum() == 0) {
        return;
    }

    const size_t originalRows = rowNum();
    const bool powerOfTwo = (originalRows & (originalRows - 1)) == 0;
    if (!powerOfTwo) {
        const auto nextPow2 = static_cast<size_t>(1) << static_cast<int>(std::ceil(std::log2(originalRows)));
        for (auto &col: _dataCols) {
            col.resize(nextPow2, 1);
        }
    }

    bitonicSort(orderFields, ascendingOrders);

    if (originalRows < rowNum()) {
        for (auto &col: _dataCols) {
            col.resize(originalRows);
        }
    }
}

int View::compareBatchSize(const std::vector<std::string> &orderFields) const {
    int totalKeyWidth = 0;
    for (const auto &field: orderFields) {
        const int idx = colIndex(field);
        if (idx < 0) {
            throw std::runtime_error("View::sort order field not found: " + field);
        }
        totalKeyWidth += _fieldWidths[idx];
    }
    if (totalKeyWidth > 62) {
        throw std::runtime_error("Pilot View::sort supports multi-column keys up to 62 packed bits.");
    }

    const int lessCap = std::max(1, Conf::IN_PATH_BMT_BUNDLE_SIZE);
    const int mutexCap = std::max(1, Conf::IN_PATH_BMT_BUNDLE_SIZE / 2);
    return std::max(1, std::min(lessCap, mutexCap));
}

void View::bitonicSort(const std::vector<std::string> &orderFields, const std::vector<bool> &ascendingOrders) {
    const int rows = static_cast<int>(rowNum());
    const int batchSize = compareBatchSize(orderFields);

    for (int k = 2; k <= rows; k <<= 1) {
        for (int j = k >> 1; j > 0; j >>= 1) {
            std::vector<PairJob> jobs;
            jobs.reserve(rows / 2);
            for (int i = 0; i < rows; ++i) {
                const int peer = i ^ j;
                if (peer <= i) {
                    continue;
                }
                jobs.push_back(PairJob{i, peer, (i & k) == 0});
            }

            const int laneNum = std::max(1, Conf::IN_PATH_LANE_NUM);
            std::vector<std::future<LaneResult>> futures;
            futures.reserve(laneNum);
            for (int lane = 0; lane < laneNum; ++lane) {
                const int start = static_cast<int>((static_cast<int64_t>(jobs.size()) * lane) / laneNum);
                const int end = static_cast<int>((static_cast<int64_t>(jobs.size()) * (lane + 1)) / laneNum);
                std::vector<PairJob> laneJobs(jobs.begin() + start, jobs.begin() + end);
                futures.push_back(std::async(std::launch::async, [this, laneJobs = std::move(laneJobs),
                                                                  &orderFields, &ascendingOrders, lane, batchSize]() {
                    return runLaneCompareSwap(laneJobs, orderFields, ascendingOrders, lane, batchSize);
                }));
            }

            for (auto &future: futures) {
                auto laneResult = future.get();
                for (size_t jobIdx = 0; jobIdx < laneResult.jobs.size(); ++jobIdx) {
                    const auto &job = laneResult.jobs[jobIdx];
                    for (size_t col = 0; col < colNum(); ++col) {
                        _dataCols[col][job.leftIndex] = laneResult.leftCols[col][jobIdx];
                        _dataCols[col][job.rightIndex] = laneResult.rightCols[col][jobIdx];
                    }
                }
            }
        }
    }
}

View::LaneResult View::runLaneCompareSwap(const std::vector<PairJob> &jobs,
                                          const std::vector<std::string> &orderFields,
                                          const std::vector<bool> &ascendingOrders,
                                          int lane,
                                          int batchSize) const {
    return LaneThreadPool::submit(lane, [this, jobs, &orderFields, &ascendingOrders, batchSize]() {
        LaneResult result;
        result.jobs = jobs;
        result.leftCols.assign(colNum(), std::vector<int64_t>(jobs.size()));
        result.rightCols.assign(colNum(), std::vector<int64_t>(jobs.size()));
        if (jobs.empty()) {
            return result;
        }

        for (size_t offset = 0; offset < jobs.size(); offset += batchSize) {
            const size_t end = std::min(jobs.size(), offset + static_cast<size_t>(batchSize));
            auto processDirection = [&](bool bitonicAscending) {
                std::vector<int> positions;
                for (size_t pos = offset; pos < end; ++pos) {
                    if (jobs[pos].ascending == bitonicAscending) {
                        positions.push_back(static_cast<int>(pos));
                    }
                }
                if (positions.empty()) {
                    return;
                }

                const int count = static_cast<int>(positions.size());
                int packedWidth = 0;
                for (const auto &field: orderFields) {
                    packedWidth += _fieldWidths[colIndex(field)];
                }

                std::vector<int64_t> leftKey(count, 0);
                std::vector<int64_t> rightKey(count, 0);
                int shift = packedWidth;
                for (size_t keyIdx = 0; keyIdx < orderFields.size(); ++keyIdx) {
                    const int colIdx = colIndex(orderFields[keyIdx]);
                    const int width = _fieldWidths[colIdx];
                    shift -= width;
                    const int64_t mask = (1LL << width) - 1;
                    const int64_t descendingFlip = ascendingOrders[keyIdx]
                                                       ? 0
                                                       : (Comm::rank() == Comm::SERVER1_RANK ? mask : 0);
                    for (int i = 0; i < count; ++i) {
                        const auto &job = jobs[positions[i]];
                        const int64_t leftPart = (_dataCols[colIdx][job.leftIndex] ^ descendingFlip) & mask;
                        const int64_t rightPart = (_dataCols[colIdx][job.rightIndex] ^ descendingFlip) & mask;
                        leftKey[i] |= leftPart << shift;
                        rightKey[i] |= rightPart << shift;
                    }
                }

                std::vector<int64_t> swapCond;
                if (bitonicAscending) {
                    swapCond = BitwiseLessOperator(&rightKey, &leftKey, packedWidth,
                                                   SecureOperator::NO_CLIENT_COMPUTE).execute()->_zis;
                } else {
                    swapCond = BitwiseLessOperator(&leftKey, &rightKey, packedWidth,
                                                   SecureOperator::NO_CLIENT_COMPUTE).execute()->_zis;
                }

                for (size_t col = 0; col < colNum(); ++col) {
                    std::vector<int64_t> left(count), right(count);
                    for (int i = 0; i < count; ++i) {
                        const auto &job = jobs[positions[i]];
                        left[i] = _dataCols[col][job.leftIndex];
                        right[i] = _dataCols[col][job.rightIndex];
                    }

                    auto swapped = BitwiseMutexOperator(&right, &left, &swapCond, _fieldWidths[col]).execute()->_zis;
                    for (int i = 0; i < count; ++i) {
                        result.leftCols[col][positions[i]] = swapped[i];
                        result.rightCols[col][positions[i]] = swapped[count + i];
                    }
                }
            };

            processDirection(true);
            processDirection(false);
        }

        return result;
    }).get();
}
