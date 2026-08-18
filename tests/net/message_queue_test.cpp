// M6.2 message queue tests: push/pop ordering, backpressure, block/drain.

#include "net/message_queue.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <optional>
#include <string>
#include <thread>

namespace mtgcpp::net {
namespace {

using namespace std::chrono_literals;

TEST(MessageQueue, PushAndPopPreserveFifoOrder) {
  MessageQueue<int> queue(4);
  ASSERT_TRUE(queue.push(1));
  ASSERT_TRUE(queue.push(2));
  ASSERT_TRUE(queue.push(3));

  const std::optional<int> a = queue.pop();
  const std::optional<int> b = queue.pop();
  const std::optional<int> c = queue.pop();
  if (a.has_value() && b.has_value() && c.has_value()) {
    EXPECT_EQ(a.value(), 1);
    EXPECT_EQ(b.value(), 2);
    EXPECT_EQ(c.value(), 3);
  } else {
    FAIL() << "expected three items in FIFO order";
  }
  EXPECT_EQ(queue.size(), 0u);
}

TEST(MessageQueue, TryPopIsNonBlocking) {
  MessageQueue<std::string> queue(2);
  EXPECT_FALSE(queue.tryPop().has_value());

  ASSERT_TRUE(queue.push("hello"));
  const std::optional<std::string> out = queue.tryPop();
  if (out.has_value()) {
    EXPECT_EQ(out.value(), "hello");
  } else {
    FAIL() << "expected the pushed item";
  }
  EXPECT_FALSE(queue.tryPop().has_value());
}

TEST(MessageQueue, PushBlocksWhenFullUntilSpaceFrees) {
  MessageQueue<int> queue(2);
  ASSERT_TRUE(queue.push(1));
  ASSERT_TRUE(queue.push(2));

  std::atomic<bool> producerUnblocked = false;
  std::thread producer([&] {
    queue.push(3); // blocks: the queue is at capacity
    producerUnblocked.store(true);
  });

  std::this_thread::sleep_for(50ms);
  EXPECT_FALSE(producerUnblocked.load());

  const std::optional<int> out = queue.pop();
  if (out.has_value()) {
    EXPECT_EQ(out.value(), 1);
  } else {
    FAIL() << "expected the first item after popping";
  }

  producer.join();
  EXPECT_TRUE(producerUnblocked.load());
  EXPECT_EQ(queue.size(), 2u); // the unblocked push landed after the pop
}

TEST(MessageQueue, PopBlocksUntilAnItemArrives) {
  MessageQueue<int> queue(4);
  std::atomic<bool> gotValue = false;
  std::atomic<int> received = 0;

  std::thread consumer([&] {
    const std::optional<int> out = queue.pop();
    if (out.has_value()) {
      received.store(out.value());
      gotValue.store(true);
    }
  });

  std::this_thread::sleep_for(30ms);
  EXPECT_FALSE(gotValue.load());

  ASSERT_TRUE(queue.push(42));
  consumer.join();

  EXPECT_TRUE(gotValue.load());
  EXPECT_EQ(received.load(), 42);
}

TEST(MessageQueue, CloseUnblocksABlockedConsumer) {
  MessageQueue<int> queue(4);
  std::atomic<bool> returned = false;

  std::thread consumer([&] {
    const std::optional<int> out = queue.pop();
    EXPECT_FALSE(out.has_value()); // closed and drained
    returned.store(true);
  });

  std::this_thread::sleep_for(30ms);
  queue.close();
  consumer.join();
  EXPECT_TRUE(returned.load());
  EXPECT_TRUE(queue.closed());
}

TEST(MessageQueue, CloseDrainsRemainingItemsBeforeReturningNullopt) {
  MessageQueue<int> queue(4);
  ASSERT_TRUE(queue.push(1));
  ASSERT_TRUE(queue.push(2));
  queue.close();

  const std::optional<int> a = queue.pop();
  const std::optional<int> b = queue.pop();
  const std::optional<int> c = queue.pop();
  if (a.has_value() && b.has_value()) {
    EXPECT_EQ(a.value(), 1);
    EXPECT_EQ(b.value(), 2);
  } else {
    FAIL() << "expected the two pending items to drain";
  }
  EXPECT_FALSE(c.has_value());
}

TEST(MessageQueue, CloseUnblocksABlockedProducer) {
  MessageQueue<int> queue(1);
  ASSERT_TRUE(queue.push(1));
  std::atomic<bool> producerReturned = false;

  std::thread producer([&] {
    const bool accepted = queue.push(2); // blocks: full
    EXPECT_FALSE(accepted);
    producerReturned.store(true);
  });

  std::this_thread::sleep_for(30ms);
  queue.close();
  producer.join();
  EXPECT_TRUE(producerReturned.load());
}

TEST(MessageQueue, PopForTimesOutWhenEmpty) {
  MessageQueue<int> queue(4);

  const std::optional<int> out = queue.popFor(20ms);

  EXPECT_FALSE(out.has_value());
}

TEST(MessageQueue, PopForReturnsAnItemWhenOneArrives) {
  MessageQueue<int> queue(4);
  std::thread producer([&] {
    std::this_thread::sleep_for(20ms);
    queue.push(7);
  });

  const std::optional<int> out = queue.popFor(2s);
  producer.join();

  if (out.has_value()) {
    EXPECT_EQ(out.value(), 7);
  } else {
    FAIL() << "expected the pushed item";
  }
}

TEST(MessageQueue, SizeReflectsPendingItems) {
  MessageQueue<int> queue(3);
  EXPECT_EQ(queue.size(), 0u);
  queue.push(1);
  queue.push(2);
  EXPECT_EQ(queue.size(), 2u);
  (void)queue.pop();
  EXPECT_EQ(queue.size(), 1u);
}

} // namespace
} // namespace mtgcpp::net
