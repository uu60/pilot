#ifndef PILOT_VIEW_H
#define PILOT_VIEW_H

#include "basis/Table.h"

#include <string>
#include <vector>

class View : public Table {
public:
    static constexpr int VALID_COL_OFFSET = -2;
    static constexpr int PADDING_COL_OFFSET = -1;
    inline static const std::string VALID_COL_NAME = "$valid";
    inline static const std::string PADDING_COL_NAME = "$padding";
    inline static const std::string EMPTY_KEY_FIELD;
    inline static const std::string EMPTY_VIEW_NAME;

    View() = default;
    View(std::vector<std::string> fieldNames, std::vector<int> fieldWidths);
    View(std::string tableName, std::vector<std::string> fieldNames, std::vector<int> fieldWidths);
    View(std::string tableName, std::vector<std::string> fieldNames, std::vector<int> fieldWidths, bool skipRedundant);

    void addRedundantCols();
    void select(const std::vector<std::string> &fieldNames);

    void sort(const std::string &orderField, bool ascendingOrder);
    void sort(const std::vector<std::string> &orderFields, const std::vector<bool> &ascendingOrders);

private:
    struct PairJob {
        int leftIndex{};
        int rightIndex{};
        bool ascending{};
    };

    struct LaneResult {
        std::vector<PairJob> jobs;
        std::vector<std::vector<int64_t>> leftCols;
        std::vector<std::vector<int64_t>> rightCols;
    };

    [[nodiscard]] int compareBatchSize(const std::vector<std::string> &orderFields) const;
    void bitonicSort(const std::vector<std::string> &orderFields, const std::vector<bool> &ascendingOrders);
    LaneResult runLaneCompareSwap(const std::vector<PairJob> &jobs,
                                  const std::vector<std::string> &orderFields,
                                  const std::vector<bool> &ascendingOrders,
                                  int lane,
                                  int batchSize) const;
};

#endif
