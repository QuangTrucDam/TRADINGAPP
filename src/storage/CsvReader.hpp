#pragma once

#include "core/Types.hpp"
#include <filesystem>
#include <fstream>
#include <string>

namespace trading_app_cli {

class CsvReader{
    public:
        explicit CsvReader(const std::filesystem::path& path);

        CsvReader(const CsvReader&) =delete;
        CsvReader& operator=(const CsvReader&) =delete;
        CsvReader(const CsvReader&&)=delete;
        CsvReader& operator=(const CsvReader&&) =delete;

        RawBarReadResult next();
        std::size_t lineNumber() const noexcept;

    private:

        enum class State{Reading, EndOfData, Failed};

        static constexpr std::size_t max_line_bytes = 4096;

        bool readLine(std::string& line);
        std::string errorContext() const;

        std::filesystem::path path_;
        std::ifstream file_;
        std::size_t line_number_{};
        std::uint64_t rows_read_{};
        State state_{State::Reading};
};

} // namespace trading_app_cli