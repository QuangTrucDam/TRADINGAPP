#include "storage/CsvReader.hpp"
#include "core/FixedPoint.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace trading_app_cli {
namespace {

bool isDigit(char value) {
    return value >= '0' && value <= '9';
}

bool isWhitespace(char value) {
    return value == ' ' || value == '\t' || value == '\r'
        || value == '\n' || value == '\v' || value == '\f';
}

std::array<std::string_view, 6> splitFields(std::string_view line) {
    if (line.empty())
        throw std::invalid_argument("blank rows are not allowed");
    if (line.starts_with("\xEF\xBB\xBF"))
        throw std::invalid_argument("UTF-8 BOM is not supported");
    if (line.find('"') != std::string_view::npos)
        throw std::invalid_argument("quoted fields are not supported");

    std::array<std::string_view, 6> fields{};
    std::size_t start = 0;
    for (std::size_t index = 0; index < fields.size(); ++index) {
        const auto comma = line.find(',', start);
        const bool last = index == fields.size() - 1;
        if ((!last && comma == std::string_view::npos)
            || (last && comma != std::string_view::npos))
            throw std::invalid_argument("expected exactly six fields");

        const auto end = last ? line.size() : comma;
        fields[index] = line.substr(start, end - start);
        if (fields[index].empty())
            throw std::invalid_argument("empty field " + std::to_string(index + 1));
        if (isWhitespace(fields[index].front()) || isWhitespace(fields[index].back()))
            throw std::invalid_argument("whitespace around field " + std::to_string(index + 1));
        if (!last) start = comma + 1;
    }
    return fields;
}

SourceLocalTime parseLocalTimestamp(std::string_view text) {
    if (text.size() != 19 || text[4] != '-' || text[7] != '-'
        || text[10] != ' ' || text[13] != ':' || text[16] != ':')
        throw std::invalid_argument("DateTime: expected yyyy-MM-dd HH:mm:ss");

    // Each part has at most four digits, so this accumulator cannot overflow.
    const auto part = [text](std::size_t start, std::size_t count) {
        int value = 0;
        for (std::size_t index = start; index < start + count; ++index) {
            if (!isDigit(text[index]))
                throw std::invalid_argument("DateTime: expected decimal digits");
            value = value * 10 + (text[index] - '0');
        }
        return value;
    };

    const int year = part(0, 4);
    const int month = part(5, 2);
    const int day = part(8, 2);
    const int hour = part(11, 2);
    const int minute = part(14, 2);
    const int second = part(17, 2);

    const std::chrono::year_month_day date{
        std::chrono::year{year},
        std::chrono::month{static_cast<unsigned>(month)},
        std::chrono::day{static_cast<unsigned>(day)}};
    if (year < 1 || !date.ok())
        throw std::invalid_argument("DateTime: invalid calendar date (years 0001-9999)");
    if (hour > 23 || minute > 59 || second > 59)
        throw std::invalid_argument("DateTime: invalid time; leap seconds are unsupported");

    return std::chrono::local_days{date} + std::chrono::hours{hour}
        + std::chrono::minutes{minute} + std::chrono::seconds{second};
}

Quantity parseVolume(std::string_view text) {
    Quantity value = 0;
    for (char character : text) {
        if (!isDigit(character))
            throw std::invalid_argument("Volume: expected nonnegative integer digits");
        const Quantity digit = character - '0';
        if (value > (std::numeric_limits<Quantity>::max() - digit) / 10)
            throw std::overflow_error("Volume: signed 64-bit integer overflow");
        value = value * 10 + digit;
    }
    return value;
}

Price parsePriceField(std::string_view text, std::string_view name) {
    try {
        return parsePrice(text); // Existing Stage 1 exact fixed-point conversion.
    } catch (const std::invalid_argument& error) {
        throw std::invalid_argument(std::string(name) + ": " + error.what());
    } catch (const std::overflow_error& error) {
        throw std::overflow_error(std::string(name) + ": " + error.what());
    }
}

void validateOhlc(const RawBarRow& row) {
    const auto lower = std::min(row.open.units, row.close.units);
    const auto upper = std::max(row.open.units, row.close.units);
    if (row.low.units > lower || upper > row.high.units)
        throw std::invalid_argument("OHLC: require low <= open/close <= high");
}

RawBarRow parseRow(std::string_view line, std::size_t lineNumber,
                   std::uint64_t sequence) {
    const auto fields = splitFields(line);
    RawBarRow row;
    row.local_start = parseLocalTimestamp(fields[0]);
    row.open = parsePriceField(fields[1], "Open");
    row.high = parsePriceField(fields[2], "High");
    row.low = parsePriceField(fields[3], "Low");
    row.close = parsePriceField(fields[4], "Close");
    row.volume = parseVolume(fields[5]);
    row.line_number = lineNumber;
    row.source_sequence = sequence;
    validateOhlc(row);
    return row;
}

} // namespace

CsvReader::CsvReader(const std::filesystem::path& path)
    : path_(path), file_(path, std::ios::in | std::ios::binary) {
    if (!file_.is_open())
        throw std::runtime_error("CSV '" + path_.string() + "': cannot open file");
}

std::string CsvReader::errorContext() const {
    return "CSV '" + path_.string() + "', line " + std::to_string(line_number_) + ": ";
}

bool CsvReader::readLine(std::string& line) {
    line.clear();
    bool started = false;
    while (true) {
        const auto character = file_.get();
        if (character == std::char_traits<char>::eof()) {
            if (file_.bad() || !file_.eof())
                throw std::runtime_error("file I/O failure while reading");
            return started; // A final row without LF is still a row.
        }

        if (!started) {
            if (line_number_ == std::numeric_limits<std::size_t>::max())
                throw std::overflow_error("physical line counter overflow");
            ++line_number_;
            started = true;
        }

        if (character == '\n') {
            // Strip CR only when it is part of an actual CRLF ending.
            if (!line.empty() && line.back() == '\r') line.pop_back();
            return true;
        }
        if (line.size() == max_line_bytes)
            throw std::invalid_argument("physical row exceeds 4096 bytes before LF");
        line.push_back(static_cast<char>(character));
    }
}

RawBarReadResult CsvReader::next() {
    if (state_ == State::EndOfData) return {};
    if (state_ == State::Failed)
        throw std::runtime_error(errorContext() + "reader is failed; construct a fresh reader");

    try {
        std::string line;
        if (!readLine(line)) {
            if (rows_read_ == 0)
                throw std::invalid_argument("empty dataset");
            state_ = State::EndOfData;
            return {};
        }
        if (rows_read_ == std::numeric_limits<std::uint64_t>::max())
            throw std::overflow_error("source sequence counter overflow");

        const auto sequence = rows_read_ + 1;
        RawBarReadResult result{BarReadStatus::Bar,
                               parseRow(line, line_number_, sequence)};
        rows_read_ = sequence; // Commit only after the complete row is valid.
        return result;
    } catch (const std::invalid_argument& error) {
        state_ = State::Failed;
        throw std::invalid_argument(errorContext() + error.what());
    } catch (const std::overflow_error& error) {
        state_ = State::Failed;
        throw std::overflow_error(errorContext() + error.what());
    } catch (const std::runtime_error& error) {
        state_ = State::Failed;
        throw std::runtime_error(errorContext() + error.what());
    } catch (...) {
        state_ = State::Failed;
        throw;
    }
}

std::size_t CsvReader::lineNumber() const noexcept {
    return line_number_;
}

} // namespace trading_app_cli
