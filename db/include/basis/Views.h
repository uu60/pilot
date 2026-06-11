#ifndef PILOT_VIEWS_H
#define PILOT_VIEWS_H

#include "basis/View.h"

class Views {
public:
    static View selectAll(Table &table);
    static View selectAllWithFieldPrefix(Table &table);
    static View selectColumns(Table &table, const std::vector<std::string> &fieldNames);
    static std::string getAliasColName(const std::string &tableName, const std::string &fieldName);
};

#endif
