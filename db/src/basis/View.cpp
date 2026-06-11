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
    if (orderFields.size() != 1) {
        throw std::runtime_error("Pilot View::sort currently supports a single order field.");
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
    int maxKeyWidth = 1;
    for (const auto &field: orderFields) {
        const int idx = colIndex(field);
        if (idx < 0) {
            throw std::runtime_error("View::sort order field not found: " + field);
        }
        maxKeyWidth = std::max(maxKeyWidth, _fieldWidths[idx]);
    }

    int maxRowWidth = 1;
    for (int width: _fieldWidths) {
        maxRowWidth = std::max(maxRowWidth, width);
    }

    const int lessCap = std::max(1, (Conf::IN_PATH_BMT_BUNDLE_SIZE * 64) / maxKeyWidth);
    const int mutexCap = std::max(1, (Conf::IN_PATH_BMT_BUNDLE_SIZE * 64) / (2 * maxRowWidth));
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
            const int count = static_cast<int>(end - offset);
            std::vector<int64_t> swapCond(count, 0);
            bool condInitialized = false;

            for (size_t keyIdx = 0; keyIdx < orderFields.size(); ++keyIdx) {
                const int colIdx = colIndex(orderFields[keyIdx]);
                std::vector<int64_t> left(count), right(count);
                for (int i = 0; i < count; ++i) {
                    const auto &job = jobs[offset + i];
                    left[i] = _dataCols[colIdx][job.leftIndex];
                    right[i] = _dataCols[colIdx][job.rightIndex];
                }

                std::vector<int64_t> keySwap(count);
                if (ascendingOrders[keyIdx]) {
                    keySwap = BitwiseLessOperator(&right, &left, _fieldWidths[colIdx],
                                                  SecureOperator::NO_CLIENT_COMPUTE).execute()->_zis;
                } else {
                    keySwap = BitwiseLessOperator(&left, &right, _fieldWidths[colIdx],
                                                  SecureOperator::NO_CLIENT_COMPUTE).execute()->_zis;
                }

                if (!condInitialized) {
                    swapCond = std::move(keySwap);
                    condInitialized = true;
                }
                // Multi-key tie breaking is intentionally deferred until Equal is chunked the same way.
            }

            for (size_t col = 0; col < colNum(); ++col) {
                std::vector<int64_t> left(count), right(count);
                for (int i = 0; i < count; ++i) {
                    const auto &job = jobs[offset + i];
                    left[i] = _dataCols[col][job.leftIndex];
                    right[i] = _dataCols[col][job.rightIndex];
                }

                auto swapped = BitwiseMutexOperator(&right, &left, &swapCond, _fieldWidths[col]).execute()->_zis;
                for (int i = 0; i < count; ++i) {
                    result.leftCols[col][offset + i] = swapped[i];
                    result.rightCols[col][offset + i] = swapped[count + i];
                }
            }
        }

        return result;
    }).get();
}
