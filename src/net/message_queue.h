// Bounded thread-safe message queue (M6.2).
//
// The memory-safety delivery rule: all async network delivery goes through a
// `std::mutex` + condition-variable queue, so game state is only ever mutated
// on the main thread (single-owner model). Network threads push inbound events
// here and never touch app state; the owner drains on its own thread.
//
// Header-only because it is a template; capacity bounds the memory a fast
// producer can buffer (backpressure: `push` blocks while full).
#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>

namespace mtgcpp::net {

template <typename T> class MessageQueue {
public:
  explicit MessageQueue(std::size_t capacity) : capacity_(capacity) {}

  MessageQueue(const MessageQueue &) = delete;
  MessageQueue &operator=(const MessageQueue &) = delete;

  // Block until the queue has room (backpressure), then append `value`. Returns
  // false when the queue is closed (e.g. during shutdown) — the caller should
  // drop the value.
  bool push(T value) {
    std::unique_lock<std::mutex> lock(mutex_);
    notFull_.wait(lock, [this] { return closed_ || queue_.size() < capacity_; });
    if (closed_) {
      return false;
    }
    queue_.push_back(std::move(value));
    notEmpty_.notify_one();
    return true;
  }

  // Block until an item is available. Returns nullopt when closed and drained.
  std::optional<T> pop() {
    std::unique_lock<std::mutex> lock(mutex_);
    notEmpty_.wait(lock, [this] { return closed_ || !queue_.empty(); });
    if (queue_.empty()) {
      return std::nullopt;
    }
    T value = std::move(queue_.front());
    queue_.pop_front();
    notFull_.notify_one();
    return value;
  }

  // Non-blocking pop; nullopt when empty (even if closed with items pending).
  std::optional<T> tryPop() {
    std::unique_lock<std::mutex> lock(mutex_);
    if (queue_.empty()) {
      return std::nullopt;
    }
    T value = std::move(queue_.front());
    queue_.pop_front();
    notFull_.notify_one();
    return value;
  }

  // Block up to `timeout` for an item. Returns nullopt on timeout, close, or a
  // drained queue.
  template <typename Rep, typename Period>
  std::optional<T> popFor(std::chrono::duration<Rep, Period> timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!notEmpty_.wait_for(lock, timeout, [this] { return closed_ || !queue_.empty(); })) {
      return std::nullopt;
    }
    if (queue_.empty()) {
      return std::nullopt;
    }
    T value = std::move(queue_.front());
    queue_.pop_front();
    notFull_.notify_one();
    return value;
  }

  std::size_t size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
  }

  bool closed() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return closed_;
  }

  // Close the queue: unblocks all waiters. `push` then fails, `pop` returns
  // the remaining items and then nullopt (drain before shutdown).
  void close() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      closed_ = true;
    }
    notEmpty_.notify_all();
    notFull_.notify_all();
  }

private:
  mutable std::mutex mutex_;
  std::condition_variable notEmpty_;
  std::condition_variable notFull_;
  std::deque<T> queue_;
  std::size_t capacity_;
  bool closed_ = false;
};

} // namespace mtgcpp::net
