#pragma once
#include <atomic>
#include <blockingconcurrentqueue.h>
#include <chrono>
#include <optional>
#include <stdexcept>

namespace trading_app_cli::live {
// One producer, one consumer per queue. close() is called by its producer after
// its final push. Count enforces an actual bound (Moodycamel capacity alone does not).
template <class T> class LiveEventQueue {
  public:
    explicit LiveEventQueue(std::size_t capacity) : queue_(capacity + 64), capacity_(capacity) {
        if (!capacity)
            throw std::invalid_argument("queue capacity must be positive");
    }
    bool push(T item) {
        if (closed_.load())
            return false;
        auto count = count_.load();
        do {
            if (count >= capacity_)
                return false;
        } while (!count_.compare_exchange_weak(count, count + 1));
        auto high = high_.load();
        while (high < count + 1 && !high_.compare_exchange_weak(high, count + 1)) {
        }
        if (!queue_.enqueue(std::optional<T>{std::move(item)})) {
            --count_;
            return false;
        }
        return true;
    }
    bool pop(T &item, std::chrono::milliseconds timeout = std::chrono::milliseconds(50)) {
        std::optional<T> value;
        if (!queue_.wait_dequeue_timed(value, timeout))
            return false;
        if (!value) {
            wake_pending_ = false;
            return false;
        }
        --count_;
        item = std::move(*value);
        return true;
    }
    // Out-of-band notification carries no input/order position. Moodycamel supports
    // this extra notifying producer; all payloads still have one ordered producer.
    void wake() {
        if (!closed_ && !wake_pending_.exchange(true)) {
            if (!queue_.enqueue(std::nullopt))
                wake_pending_ = false;
        }
    }
    void close() {
        if (!closed_.exchange(true))
            queue_.enqueue(std::nullopt); // wake idle consumer
    }
    bool drained() const { return closed_.load() && count_.load() == 0; }
    std::size_t highWater() const { return high_.load(); }
    std::size_t depth() const { return count_.load(); }

  private:
    moodycamel::BlockingConcurrentQueue<std::optional<T>> queue_;
    std::size_t capacity_;
    std::atomic<std::size_t> count_{0}, high_{0};
    std::atomic<bool> closed_{false}, wake_pending_{false};
};
} // namespace trading_app_cli::live
