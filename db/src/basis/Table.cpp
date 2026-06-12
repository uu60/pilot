#include "basis/Table.h"

#include <algorithm>
#include <stdexcept>

Table::Table(std::string tableName, std::vector<std::string> fieldNames,
             std::vector<int> fieldWidths, std::string keyField)
    : _tableName(std::move(tableName)),
      _fieldNames(std::move(fieldNames)),
      _keyField(std::move(keyField)),
      _fieldWidths(std::move(fieldWidths)) {
    if (!_keyField.empty()) {
        _fieldNames.push_back(BUCKET_TAG_PREFIX + _keyField);
        _fieldWidths.push_back(32);
    }
    for (int width: _fieldWidths) {
        if (width <= 0 || width > 64) {
            throw std::runtime_error("Pilot supports field width in range [1, 64].");
        }
        _maxWidth = std::max(_maxWidth, width);
    }
    _dataCols.resize(_fieldNames.size());
}

bool Table::insert(const std::vector<int64_t> &row) {
    if (row.size() != colNum()) {
        throw std::runtime_error("Table::insert column count mismatch.");
    }
    for (size_t col = 0; col < row.size(); ++col) {
        _dataCols[col].push_back(row[col]);
    }
    return true;
}

int Table::colIndex(const std::string &colName) const {
    for (int i = 0; i < static_cast<int>(_fieldNames.size()); ++i) {
        if (_fieldNames[i] == colName) {
            return i;
        }
    }
    return -1;
}

size_t Table::colNum() const {
    return _dataCols.size();
}

size_t Table::rowNum() const {
    return _dataCols.empty() ? 0 : _dataCols[0].size();
}
