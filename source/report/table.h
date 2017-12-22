//
// PROJECT:         Aspia
// FILE:            report/table.h
// LICENSE:         Mozilla Public License Version 2.0
// PROGRAMMERS:     Dmitry Chapyshev (dmitry@aspia.ru)
//

#ifndef _ASPIA_REPORT__TABLE_H
#define _ASPIA_REPORT__TABLE_H

#include "report/group.h"
#include "report/column_list.h"
#include "report/row.h"

namespace aspia {

class Output;
class Category;

class Table
{
public:
    Table(Table&& other);
    ~Table();

    static Table Create(Output* output, Category* category);

    void AddColumns(const ColumnList& column_list);

    Group AddGroup(std::string_view name);
    void AddParam(std::string_view param, const Value& value);
    Row AddRow();

    Table& operator=(Table&& other);

private:
    Table(Output* output, Category* category);

    Output* output_;

    DISALLOW_COPY_AND_ASSIGN(Table);
};

} // namespace aspia

#endif // _ASPIA_REPORT__TABLE_H