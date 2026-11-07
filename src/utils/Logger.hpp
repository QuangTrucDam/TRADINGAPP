#pragma once

#include <string>
#include <cstdint>
#include <string_view>
#include <mutex>

namespace trading_app_cli{

class Logger final {
    public: static Logger& instance();

        void info(std::string_view message);
        void error(std::string_view message);

        Logger(const Logger&) =delete;
        Logger& operator=(const Logger&) = delete;
        Logger (Logger&&) =delete;
        Logger& operator=(Logger&&) = delete;
    private:
        Logger() = default;
        void write(std::string_view level, std::string_view message);
        std::mutex mutex_;
};

} // namspace trading_app_cli