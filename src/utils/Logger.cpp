#include "utils/Logger.hpp"
#include <iostream>
namespace trading_app_cli {
Logger& Logger::instance() { static Logger logger; return logger; }
void Logger::write(std::string_view level, std::string_view message) {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::clog << '[' << level << "] " << message << '\n';
}
void Logger::info(std::string_view message) { write("INFO", message); }
void Logger::error(std::string_view message) { write("ERROR", message); }
} // namespace trading_app_cli
