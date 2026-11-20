#pragma once

#include "DLCCommon.h"

// DLC:csv: comma-separated text (RFC 4180) to rows and back. A field is text: the library never guesses that "007" is
// a number, so what a program reads is what the file says, and `to_number` turns a field into a number where the
// program wants one.

namespace cuff
{

    namespace csv
    {

        // The separator argument: one character, which cannot be a quote or a line break.
        inline char separatorArg(const char *fn, std::vector<Value> &args, size_t i, const SourceLocation &loc)
        {
            if (args.size() <= i)
                return ',';
            const std::string &s = expectStr(fn, args, i, loc);
            if (s.size() != 1 || s[0] == '"' || s[0] == '\n' || s[0] == '\r')
                throw ValueError(std::string(fn) + "() expects the separator to be one character other than a quote or a line break, got \"" + s +
                                     "\"",
                                 loc);
            return s[0];
        }

        // Reads `text` into rows of fields. A field in quotes may hold the separator, line breaks and quotes (written
        // twice); after its closing quote only a separator or the end of the line may follow. A blank line is no row.
        // Line breaks are LF, CRLF or CR, and a UTF-8 byte order mark at the start is skipped.
        CUFF_NOINLINE inline std::vector<std::vector<std::string>> parse(const char *fn, const std::string &text, char sep, const SourceLocation &loc)
        {
            std::vector<std::vector<std::string>> rows;
            std::vector<std::string> row;
            std::string field;
            size_t i = text.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0;
            const size_t n = text.size();
            size_t line = 1;
            bool rowHasContent = false; // a row of one empty unquoted field is a blank line
            size_t items = 0;
            auto endField = [&]
            {
                row.push_back(std::move(field));
                field.clear();
                if (++items > limits::kMaxCollectionItems)
                    ensureItemCount(items, loc);
            };
            auto endRow = [&]
            {
                endField();
                if (rowHasContent || row.size() > 1)
                    rows.push_back(std::move(row));
                row.clear();
                rowHasContent = false;
            };
            while (i < n)
            {
                const char c = text[i];
                if (c == '"' && field.empty())
                {
                    // A quoted field.
                    const size_t startLine = line;
                    rowHasContent = true;
                    ++i;
                    for (;;)
                    {
                        if (i >= n)
                            throw ValueError(std::string(fn) + "() found a quoted field that is never closed; it begins on line " + std::to_string(startLine),
                                             loc);
                        if (text[i] == '"')
                        {
                            if (i + 1 < n && text[i + 1] == '"')
                            {
                                field += '"';
                                i += 2;
                                continue;
                            }
                            ++i;
                            break;
                        }
                        if (text[i] == '\n' || (text[i] == '\r' && !(i + 1 < n && text[i + 1] == '\n')))
                            ++line;
                        field += text[i++];
                    }
                    if (i < n && text[i] != sep && text[i] != '\n' && text[i] != '\r')
                        throw ValueError(std::string(fn) + "() expects a separator or the end of the line after a closing quote, on line " +
                                             std::to_string(line),
                                         loc);
                    continue;
                }
                if (c == sep)
                {
                    rowHasContent = true;
                    endField();
                    ++i;
                    continue;
                }
                if (c == '\n' || c == '\r')
                {
                    endRow();
                    i += (c == '\r' && i + 1 < n && text[i + 1] == '\n') ? 2 : 1;
                    ++line;
                    continue;
                }
                rowHasContent = true;
                field += c;
                ++i;
            }
            if (rowHasContent || !field.empty() || !row.empty())
                endRow();
            return rows;
        }

        inline bool needsQuotes(const std::string &s, char sep)
        {
            if (s.empty())
                return false;
            if (s.front() == ' ' || s.back() == ' ')
                return true;
            for (char c : s)
                if (c == sep || c == '"' || c == '\n' || c == '\r')
                    return true;
            return false;
        }

        // The text a value is written as: a str as it is, a number as `print` writes it, a boolean as true or false,
        // `empty` as nothing.
        inline std::string fieldText(const char *fn, const Value &v, const SourceLocation &loc)
        {
            if (v.isStr())
                return v.asStr();
            if (v.isNumber() || v.isBool())
                return v.toDisplayString();
            if (v.isEmpty())
                return std::string();
            throw TypeError(std::string(fn) + "() can write a str, a number, a boolean or empty as a field, got " + valueTypeName(v.type()), loc);
        }

        inline void writeField(std::string &out, const std::string &s, char sep)
        {
            if (!needsQuotes(s, sep))
            {
                out += s;
                return;
            }
            out += '"';
            for (char c : s)
            {
                if (c == '"')
                    out += '"';
                out += c;
            }
            out += '"';
        }

        CUFF_NOINLINE inline void writeRow(const char *fn, std::string &out, const std::vector<Value> &fields, char sep, const SourceLocation &loc)
        {
            for (size_t k = 0; k < fields.size(); ++k)
            {
                if (k)
                    out += sep;
                writeField(out, fieldText(fn, fields[k], loc), sep);
            }
            // A row of one empty field would read back as a blank line, which is no row: it is written in quotes.
            if (fields.size() == 1 && fieldText(fn, fields[0], loc).empty())
                out += "\"\"";
            out += '\n';
            ensureStringSize(out.size(), loc);
        }

    }

    inline void registerCsvDLC(std::unordered_map<std::string, NativeFn> &reg)
    {
        // Plain functions, not one closure type each: std::function then needs one handler for all of them, which
        // keeps the library small.
        auto add = [&reg](const char *name, Value (*fn)(std::vector<Value> &, const SourceLocation &)) { reg[name] = fn; };

        add("csv_parse", +[](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgRange("csv_parse", args, 1, 2, loc);
            const std::string &text = expectStr("csv_parse", args, 0, loc);
            const char sep = csv::separatorArg("csv_parse", args, 1, loc);
            auto rows = csv::parse("csv_parse", text, sep, loc);
            auto out = std::make_shared<ValueList>();
            out->items.reserve(rows.size());
            for (auto &row : rows)
            {
                auto fields = std::make_shared<ValueList>();
                fields->items.reserve(row.size());
                for (std::string &f : row)
                    fields->items.push_back(Value::makeStr(std::move(f)));
                out->items.push_back(Value::makeList(std::move(fields)));
            }
            return Value::makeList(std::move(out));
        });

        // The first row names the fields, and every other row is a map from those names. A row with more or fewer
        // fields than the first one is a mistake in the file, and is reported with its number.
        add("csv_parse_records", +[](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgRange("csv_parse_records", args, 1, 2, loc);
            const std::string &text = expectStr("csv_parse_records", args, 0, loc);
            const char sep = csv::separatorArg("csv_parse_records", args, 1, loc);
            auto rows = csv::parse("csv_parse_records", text, sep, loc);
            auto out = std::make_shared<ValueList>();
            if (rows.empty())
                return Value::makeList(std::move(out));
            const std::vector<std::string> &header = rows.front();
            for (size_t a = 0; a < header.size(); ++a)
                for (size_t b = a + 1; b < header.size(); ++b)
                    if (header[a] == header[b])
                        throw ValueError("csv_parse_records() found the field name \"" + header[a] + "\" twice in the first row", loc);
            out->items.reserve(rows.size() - 1);
            for (size_t r = 1; r < rows.size(); ++r)
            {
                if (rows[r].size() != header.size())
                    throw ValueError("csv_parse_records() found " + std::to_string(rows[r].size()) + " field(s) in row " + std::to_string(r + 1) +
                                         ", and the first row names " + std::to_string(header.size()),
                                     loc);
                auto record = std::make_shared<ValueMap>();
                record->reserve(header.size());
                for (size_t k = 0; k < header.size(); ++k)
                    record->set(header[k], Value::makeStr(std::move(rows[r][k])));
                out->items.push_back(Value::makeMap(std::move(record)));
            }
            return Value::makeList(std::move(out));
        });

        add("csv_write", +[](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgRange("csv_write", args, 1, 2, loc);
            const ValueList &rows = expectList("csv_write", args, 0, loc);
            const char sep = csv::separatorArg("csv_write", args, 1, loc);
            std::string out;
            for (size_t r = 0; r < rows.items.size(); ++r)
            {
                if (!rows.items[r].isList())
                    throw TypeError("csv_write() expects a list of rows, each a list of fields; row " + std::to_string(r + 1) + " is a " +
                                        valueTypeName(rows.items[r].type()),
                                    loc);
                csv::writeRow("csv_write", out, rows.items[r].asList()->items, sep, loc);
            }
            return Value::makeStr(std::move(out));
        });

        // A header row of every key, in the order the keys first appear, then one row for each map; a key a map does
        // not have is an empty field.
        add("csv_write_records", +[](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgRange("csv_write_records", args, 1, 2, loc);
            const ValueList &records = expectList("csv_write_records", args, 0, loc);
            const char sep = csv::separatorArg("csv_write_records", args, 1, loc);
            std::vector<std::string> header;
            std::unordered_set<std::string> seen;
            for (size_t r = 0; r < records.items.size(); ++r)
            {
                if (!records.items[r].isMap())
                    throw TypeError("csv_write_records() expects a list of maps; item " + std::to_string(r + 1) + " is a " +
                                        valueTypeName(records.items[r].type()),
                                    loc);
                for (const std::string &key : records.items[r].asMap()->keys())
                    if (seen.insert(key).second)
                        header.push_back(key);
            }
            std::string out;
            if (header.empty())
                return Value::makeStr(std::move(out));
            std::vector<Value> row;
            for (const std::string &key : header)
                row.push_back(Value::makeStr(key));
            csv::writeRow("csv_write_records", out, row, sep, loc);
            for (const Value &record : records.items)
            {
                row.clear();
                for (const std::string &key : header)
                {
                    const Value *v = record.asMap()->get(key);
                    row.push_back(v ? *v : Value());
                }
                csv::writeRow("csv_write_records", out, row, sep, loc);
            }
            return Value::makeStr(std::move(out));
        });
    }

}
