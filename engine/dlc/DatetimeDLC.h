#pragma once

#include "DLCCommon.h"

#include <chrono>
#include <ctime>
#include <limits>

namespace cuff
{

    // Calendar arithmetic on the proleptic Gregorian calendar, with no time zone database: a zone is a whole number of
    // minutes east of UTC that the script passes in, and `datetime_offset` reads the machine's own offset for an instant.
    namespace datetimeutil
    {

        // Years 0001-01-01 00:00:00 to 9999-12-31 23:59:59 UTC, as seconds since the Unix epoch.
        constexpr double kMinSeconds = -62135596800.0;
        constexpr double kMaxSeconds = 253402300799.0;
        constexpr long long kMaxOffsetMinutes = 24 * 60;

        inline long long daysFromCivil(long long y, unsigned m, unsigned d)
        {
            y -= m <= 2;
            const long long era = (y >= 0 ? y : y - 399) / 400;
            const unsigned yoe = static_cast<unsigned>(y - era * 400);
            const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
            const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
            return era * 146097 + static_cast<long long>(doe) - 719468;
        }

        struct Civil
        {
            long long year;
            unsigned month;
            unsigned day;
        };

        inline Civil civilFromDays(long long z)
        {
            z += 719468;
            const long long era = (z >= 0 ? z : z - 146096) / 146097;
            const unsigned doe = static_cast<unsigned>(z - era * 146097);
            const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
            const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
            const unsigned mp = (5 * doy + 2) / 153;
            const unsigned d = doy - (153 * mp + 2) / 5 + 1;
            const unsigned m = mp < 10 ? mp + 3 : mp - 9;
            return {static_cast<long long>(yoe) + era * 400 + (m <= 2), m, d};
        }

        inline bool isLeap(long long y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

        inline unsigned daysInMonth(long long y, unsigned m)
        {
            static const unsigned table[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
            return m == 2 && isLeap(y) ? 29 : table[m - 1];
        }

        inline long long floorDiv(long long a, long long b)
        {
            long long q = a / b;
            return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
        }

        struct Parts
        {
            long long year;
            unsigned month, day, hour, minute, second;
            unsigned weekday; // 1 = Monday ... 7 = Sunday
            unsigned yearday; // 1 ... 366
        };

        // `local` is whole seconds already shifted by the zone's offset.
        inline Parts partsOf(long long local)
        {
            const long long days = floorDiv(local, 86400);
            const long long rest = local - days * 86400;
            const Civil c = civilFromDays(days);
            Parts p;
            p.year = c.year;
            p.month = c.month;
            p.day = c.day;
            p.hour = static_cast<unsigned>(rest / 3600);
            p.minute = static_cast<unsigned>(rest % 3600 / 60);
            p.second = static_cast<unsigned>(rest % 60);
            p.weekday = static_cast<unsigned>(((days % 7) + 7 + 3) % 7) + 1; // 1970-01-01 was a Thursday
            p.yearday = static_cast<unsigned>(days - daysFromCivil(c.year, 1, 1)) + 1;
            return p;
        }

        // The instant as whole seconds, shifted into the zone, or a ValueError when it is outside years 1 to 9999.
        inline long long shifted(const char *fn, double seconds, long long offsetMinutes, const SourceLocation &loc)
        {
            if (!std::isfinite(seconds))
                throw ValueError(std::string(fn) + "() expects a finite number of seconds", loc);
            const double floored = std::floor(seconds);
            const double local = floored + static_cast<double>(offsetMinutes) * 60.0;
            if (local < kMinSeconds || local > kMaxSeconds)
                throw ValueError(std::string(fn) + "() handles years 1 to 9999 only, got " + formatCuffNumber(seconds), loc);
            return static_cast<long long>(local);
        }

        inline long long offsetArg(const char *fn, std::vector<Value> &args, size_t i, const SourceLocation &loc)
        {
            if (args.size() <= i)
                return 0;
            const long long minutes = expectWhole(fn, args, i, loc);
            if (minutes <= -kMaxOffsetMinutes || minutes >= kMaxOffsetMinutes)
                throw ValueError(std::string(fn) + "() expects the offset in minutes, less than a day either way, got " + std::to_string(minutes), loc);
            return minutes;
        }

        inline void pad(std::string &out, long long value, int width)
        {
            std::string digits = std::to_string(value);
            if (static_cast<int>(digits.size()) < width)
                out.append(static_cast<size_t>(width) - digits.size(), '0');
            out += digits;
        }

        // The machine's offset from UTC at an instant, in minutes east; 0 when the system cannot say.
        inline long long localOffsetMinutes(double seconds)
        {
            if (!std::isfinite(seconds) || seconds < kMinSeconds || seconds > kMaxSeconds)
                return 0;
            // A 32-bit time_t (32-bit Android, for one) ends in 2038; converting a later instant to it is undefined.
            const double whole = std::floor(seconds);
            if (whole < static_cast<double>(std::numeric_limits<std::time_t>::min()) ||
                whole > static_cast<double>(std::numeric_limits<std::time_t>::max()))
                return 0;
            const std::time_t t = static_cast<std::time_t>(whole);
            std::tm lt;
#ifdef _WIN32
            if (localtime_s(&lt, &t) != 0)
                return 0;
#else
            if (!localtime_r(&t, &lt))
                return 0;
#endif
            const long long asIfUtc = daysFromCivil(lt.tm_year + 1900, static_cast<unsigned>(lt.tm_mon + 1), static_cast<unsigned>(lt.tm_mday)) * 86400 +
                                      lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec;
            return std::llround(static_cast<double>(asIfUtc - static_cast<long long>(t)) / 60.0);
        }

        // Which piece of a pattern comes next: a token, or one literal run.
        enum class Tok
        {
            Year,   // YYYY
            Month,  // MM
            Day,    // DD
            Hour,   // HH
            Minute, // mm
            Second, // ss
            Milli,  // SSS
            Zone,   // Z
            Text
        };

        struct Piece
        {
            Tok tok;
            std::string text; // for Tok::Text
        };

        // "YYYY-MM-DD[T]HH:mm:ss.SSSZ": the tokens above, and text in square brackets or any other character as itself.
        inline std::vector<Piece> splitPattern(const char *fn, const std::string &pat, const SourceLocation &loc)
        {
            std::vector<Piece> out;
            auto text = [&](const std::string &s)
            {
                if (!out.empty() && out.back().tok == Tok::Text)
                    out.back().text += s;
                else
                    out.push_back({Tok::Text, s});
            };
            size_t i = 0;
            while (i < pat.size())
            {
                auto at = [&](const char *word) { return pat.compare(i, std::strlen(word), word) == 0; };
                if (pat[i] == '[')
                {
                    const size_t close = pat.find(']', i + 1);
                    if (close == std::string::npos)
                        throw ValueError(std::string(fn) + "() has a '[' in the pattern with no closing ']'", loc);
                    text(pat.substr(i + 1, close - i - 1));
                    i = close + 1;
                }
                else if (at("YYYY")) { out.push_back({Tok::Year, ""}); i += 4; }
                else if (at("SSS")) { out.push_back({Tok::Milli, ""}); i += 3; }
                else if (at("MM")) { out.push_back({Tok::Month, ""}); i += 2; }
                else if (at("DD")) { out.push_back({Tok::Day, ""}); i += 2; }
                else if (at("HH")) { out.push_back({Tok::Hour, ""}); i += 2; }
                else if (at("mm")) { out.push_back({Tok::Minute, ""}); i += 2; }
                else if (at("ss")) { out.push_back({Tok::Second, ""}); i += 2; }
                else if (pat[i] == 'Z') { out.push_back({Tok::Zone, ""}); i += 1; }
                else
                {
                    text(std::string(1, pat[i]));
                    ++i;
                }
            }
            return out;
        }

        inline void appendZone(std::string &out, long long offsetMinutes)
        {
            if (offsetMinutes == 0)
            {
                out += 'Z';
                return;
            }
            out += offsetMinutes < 0 ? '-' : '+';
            const long long m = offsetMinutes < 0 ? -offsetMinutes : offsetMinutes;
            pad(out, m / 60, 2);
            out += ':';
            pad(out, m % 60, 2);
        }

        // Exactly `width` digits at text[pos], or -1.
        inline long long digitsAt(const std::string &text, size_t pos, size_t width)
        {
            if (pos + width > text.size())
                return -1;
            long long v = 0;
            for (size_t k = 0; k < width; ++k)
            {
                const char c = text[pos + k];
                if (c < '0' || c > '9')
                    return -1;
                v = v * 10 + (c - '0');
            }
            return v;
        }

    } // namespace datetimeutil

    inline void registerDatetimeDLC(std::unordered_map<std::string, NativeFn> &reg)
    {
        using namespace datetimeutil;

        reg["datetime_offset"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgRange("datetime_offset", args, 0, 1, loc);
            double seconds;
            if (args.empty())
                seconds = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
            else
                seconds = expectNumber("datetime_offset", args, 0, loc);
            return Value::makeNumber(static_cast<double>(localOffsetMinutes(seconds)));
        };

        reg["datetime_parts"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgRange("datetime_parts", args, 1, 2, loc);
            const double seconds = expectNumber("datetime_parts", args, 0, loc);
            const long long offset = offsetArg("datetime_parts", args, 1, loc);
            const Parts p = partsOf(shifted("datetime_parts", seconds, offset, loc));
            auto map = std::make_shared<ValueMap>();
            map->set("year", Value::makeNumber(static_cast<double>(p.year)));
            map->set("month", Value::makeNumber(p.month));
            map->set("day", Value::makeNumber(p.day));
            map->set("hour", Value::makeNumber(p.hour));
            map->set("minute", Value::makeNumber(p.minute));
            map->set("second", Value::makeNumber(p.second));
            map->set("weekday", Value::makeNumber(p.weekday));
            map->set("yearday", Value::makeNumber(p.yearday));
            return Value::makeMap(map);
        };

        reg["datetime_make"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgRange("datetime_make", args, 3, 7, loc);
            const long long year = expectWhole("datetime_make", args, 0, loc);
            const long long month = expectWhole("datetime_make", args, 1, loc);
            const long long day = expectWhole("datetime_make", args, 2, loc);
            const long long hour = args.size() > 3 ? expectWhole("datetime_make", args, 3, loc) : 0;
            const long long minute = args.size() > 4 ? expectWhole("datetime_make", args, 4, loc) : 0;
            const double second = args.size() > 5 ? expectNumber("datetime_make", args, 5, loc) : 0.0;
            const long long offset = offsetArg("datetime_make", args, 6, loc);
            // A date that does not exist is a condition of the data, not a mistake in the script: `empty`.
            if (year < 1 || year > 9999 || month < 1 || month > 12 || day < 1 ||
                day > static_cast<long long>(daysInMonth(year, static_cast<unsigned>(month))) ||
                hour < 0 || hour > 23 || minute < 0 || minute > 59 || !(second >= 0.0 && second < 60.0))
                return Value::makeEmpty();
            const long long days = daysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
            const double whole = static_cast<double>(days * 86400 + hour * 3600 + minute * 60 - offset * 60);
            return Value::makeNumber(whole + second);
        };

        reg["datetime_format"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgRange("datetime_format", args, 2, 3, loc);
            const double seconds = expectNumber("datetime_format", args, 0, loc);
            const std::string &pattern = expectStr("datetime_format", args, 1, loc);
            const long long offset = offsetArg("datetime_format", args, 2, loc);
            const Parts p = partsOf(shifted("datetime_format", seconds, offset, loc));
            // Nearest millisecond, kept inside the second it belongs to.
            long long milli = static_cast<long long>(std::floor((seconds - std::floor(seconds)) * 1000.0 + 0.5));
            if (milli > 999)
                milli = 999;
            std::string out;
            for (const Piece &piece : splitPattern("datetime_format", pattern, loc))
            {
                switch (piece.tok)
                {
                case Tok::Year: pad(out, p.year, 4); break;
                case Tok::Month: pad(out, p.month, 2); break;
                case Tok::Day: pad(out, p.day, 2); break;
                case Tok::Hour: pad(out, p.hour, 2); break;
                case Tok::Minute: pad(out, p.minute, 2); break;
                case Tok::Second: pad(out, p.second, 2); break;
                case Tok::Milli: pad(out, milli, 3); break;
                case Tok::Zone: appendZone(out, offset); break;
                case Tok::Text: out += piece.text; break;
                }
                ensureStringSize(out.size(), loc);
            }
            return Value::makeStr(std::move(out));
        };

        reg["datetime_parse"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgRange("datetime_parse", args, 2, 3, loc);
            const std::string &text = expectStr("datetime_parse", args, 0, loc);
            const std::string &pattern = expectStr("datetime_parse", args, 1, loc);
            long long offset = offsetArg("datetime_parse", args, 2, loc);
            long long year = 1970, month = 1, day = 1, hour = 0, minute = 0, second = 0, milli = 0;
            size_t pos = 0;
            for (const Piece &piece : splitPattern("datetime_parse", pattern, loc))
            {
                long long *slot = nullptr;
                size_t width = 0;
                switch (piece.tok)
                {
                case Tok::Year: slot = &year; width = 4; break;
                case Tok::Month: slot = &month; width = 2; break;
                case Tok::Day: slot = &day; width = 2; break;
                case Tok::Hour: slot = &hour; width = 2; break;
                case Tok::Minute: slot = &minute; width = 2; break;
                case Tok::Second: slot = &second; width = 2; break;
                case Tok::Milli: slot = &milli; width = 3; break;
                case Tok::Zone:
                    if (pos < text.size() && text[pos] == 'Z')
                    {
                        offset = 0;
                        ++pos;
                    }
                    else
                    {
                        const bool minus = pos < text.size() && text[pos] == '-';
                        if (pos >= text.size() || (text[pos] != '+' && !minus) || pos + 6 > text.size() || text[pos + 3] != ':')
                            return Value::makeEmpty();
                        const long long hh = digitsAt(text, pos + 1, 2), mm = digitsAt(text, pos + 4, 2);
                        if (hh < 0 || mm < 0 || hh > 23 || mm > 59)
                            return Value::makeEmpty();
                        offset = (minus ? -1 : 1) * (hh * 60 + mm);
                        pos += 6;
                    }
                    break;
                case Tok::Text:
                    if (text.compare(pos, piece.text.size(), piece.text) != 0)
                        return Value::makeEmpty();
                    pos += piece.text.size();
                    break;
                }
                if (slot)
                {
                    const long long v = digitsAt(text, pos, width);
                    if (v < 0)
                        return Value::makeEmpty();
                    *slot = v;
                    pos += width;
                }
            }
            if (pos != text.size())
                return Value::makeEmpty();
            if (year < 1 || month < 1 || month > 12 || day < 1 || day > static_cast<long long>(daysInMonth(year, static_cast<unsigned>(month))) ||
                hour > 23 || minute > 59 || second > 59)
                return Value::makeEmpty();
            const long long days = daysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
            const double whole = static_cast<double>(days * 86400 + hour * 3600 + minute * 60 + second - offset * 60);
            return Value::makeNumber(whole + static_cast<double>(milli) / 1000.0);
        };
    }

}
