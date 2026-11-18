#pragma once
#include <memory>
#include <string>
namespace trading_app_desktop {
// GUI-thread-only admission gate. Feed observation does not acquire it.
class EngineGate {
  public:
    bool acquire(const std::string &owner) {
        if (!owner_.empty())
            return false;
        owner_ = owner;
        return true;
    }
    void release(const std::string &owner) {
        if (owner_ == owner)
            owner_.clear();
    }
    bool busy() const { return !owner_.empty(); }
    const std::string &owner() const { return owner_; }

  private:
    std::string owner_;
};
using SharedEngineGate = std::shared_ptr<EngineGate>;
} // namespace trading_app_desktop
