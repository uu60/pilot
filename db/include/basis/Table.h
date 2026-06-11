#ifndef PILOT_TABLE_H
#define PILOT_TABLE_H

#include <cstdint>
#include <string>
#include <vector>

class Table {
public:
    inline static const std::string BUCKET_TAG_PREFIX = "$tag:";

    std::string _tableName;
    std::vector<std::string> _fieldNames;
    std::string _keyField;
    std::vector<int> _fieldWidths;
    std::vector<std::vector<int64_t>> _dataCols;
    int _maxWidth{};

    Table() = default;
    Table(std::string tableName, std::vector<std::string> fieldNames,
          std::vector<int> fieldWidths, std::string keyField = "");

    bool insert(const std::vector<int64_t> &row);

    [[nodiscard]] int colIndex(const std::string &colName) const;
    [[nodiscard]] size_t colNum() const;
    [[nodiscard]] size_t rowNum() const;
};

#endif
