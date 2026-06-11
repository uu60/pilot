#include "basis/Views.h"

#include "comm/Comm.h"

#include <algorithm>

View Views::selectAll(Table &table) {
    View view(table._tableName, table._fieldNames, table._fieldWidths);
    view._dataCols = table._dataCols;
    view._dataCols.emplace_back(table.rowNum(), Comm::rank());
    view._dataCols.emplace_back(table.rowNum(), 0);
    return view;
}

View Views::selectAllWithFieldPrefix(Table &table) {
    std::vector<std::string> fieldNames;
    fieldNames.reserve(table._fieldNames.size());
    for (const auto &fieldName: table._fieldNames) {
        fieldNames.push_back(getAliasColName(table._tableName, fieldName));
    }

    View view(table._tableName, fieldNames, table._fieldWidths);
    view._dataCols = table._dataCols;
    view._dataCols.emplace_back(table.rowNum(), Comm::rank());
    view._dataCols.emplace_back(table.rowNum(), 0);
    return view;
}

View Views::selectColumns(Table &table, const std::vector<std::string> &fieldNames) {
    std::vector<int> indices;
    std::vector<int> widths;
    indices.reserve(fieldNames.size());
    widths.reserve(fieldNames.size());
    for (const auto &fieldName: fieldNames) {
        const int idx = table.colIndex(fieldName);
        if (idx >= 0) {
            indices.push_back(idx);
            widths.push_back(table._fieldWidths[idx]);
        }
    }

    View view(table._tableName, fieldNames, widths);
    for (int out = 0; out < static_cast<int>(indices.size()); ++out) {
        view._dataCols[out] = table._dataCols[indices[out]];
    }
    view._dataCols[view.colNum() + View::VALID_COL_OFFSET] = std::vector<int64_t>(table.rowNum(), Comm::rank());
    view._dataCols[view.colNum() + View::PADDING_COL_OFFSET] = std::vector<int64_t>(table.rowNum(), 0);
    return view;
}

std::string Views::getAliasColName(const std::string &tableName, const std::string &fieldName) {
    return tableName.empty() ? fieldName : tableName + "." + fieldName;
}
