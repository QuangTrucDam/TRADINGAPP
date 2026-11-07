#pragma once
#include "utils/Logger.hpp"
#include <string>
#include <cstdint>
#include <string_view>
#include <chrono>

namespace trading_app_cli{

class ScopedTimer final {
    public: 
        explicit ScopedTimer(std::string label)
        : label_(std::move(label)), started_at_(std::chrono::steady_clock::now()) {}
    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;
    ScopedTimer(ScopedTimer&&) = delete;
    ScopedTimer& operator=(ScopedTimer&&) = delete;
    ~ScopedTimer() noexcept {
        try {
            const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - started_at_).count();
            Logger::instance().info(label_ + ": " + std::to_string(elapsed) + " us");
        } catch (...) { /* Diagnostics must not interrupt exception unwinding. */ }
    }
    private:
        std::string label_;
        std::chrono::steady_clock::time_point started_at_;

};

} // namspace trading_app_cli