/**
 * @file tests/unit/test-streamhub-queue.cpp
 * @brief Verify bounded submission preserves ownership and per-session cleanup.
 */
#include "../tests_common.h"
#include "src/input.h"
#include "src/thread_safe.h"

#include <future>
#include <thread>

TEST(StreamHubQueueTest, FullOrStoppedQueuePreservesRejectedOwnership) {
  safe::queue_t<std::unique_ptr<int>> queue(1);
  auto first = std::make_unique<int>(1), second = std::make_unique<int>(2);
  ASSERT_TRUE(queue.try_raise(std::move(first)));
  EXPECT_FALSE(first);
  EXPECT_FALSE(queue.try_raise(std::move(second)));
  ASSERT_TRUE(second);
  EXPECT_EQ(*second, 2);
  queue.stop();
  EXPECT_FALSE(queue.try_raise(std::move(second)));
  EXPECT_TRUE(second);
  queue.discard_if([](const auto &) {
    return true;
  });
}

TEST(StreamHubQueueTest, SelectiveDiscardReleasesOnlyMatchingOwner) {
  safe::queue_t<std::shared_ptr<int>> queue(3);
  auto first = std::make_shared<int>(1), second = std::make_shared<int>(2);
  std::weak_ptr<int> retired = first, retained = second;
  queue.try_raise(std::move(first));
  queue.try_raise(std::move(second));
  queue.discard_if([](const auto &p) {
    return *p == 1;
  });
  EXPECT_TRUE(retired.expired());
  EXPECT_FALSE(retained.expired());
  auto item = queue.pop(std::chrono::milliseconds(0));
  ASSERT_TRUE(item);
  EXPECT_EQ(*item, 2);
}

TEST(StreamHubQueueTest, StopWakesWaitingConsumer) {
  safe::queue_t<int> queue;
  std::atomic_bool ended {false};
  std::jthread worker([&] {
    EXPECT_FALSE(queue.pop());
    ended.store(true);
  });
  queue.stop();
  worker.join();
  EXPECT_TRUE(ended.load());
  EXPECT_FALSE(queue.running());
  EXPECT_FALSE(queue.peek());
}

TEST(StreamHubQueueTest, ProducerWakesOnPopDiscardAndCancellation) {
  using namespace std::chrono_literals;
  safe::queue_t<int> queue(1);
  ASSERT_TRUE(queue.try_raise(1));
  auto waiting = std::async(std::launch::async, [&] {
    return queue.wait_space({});
  });
  EXPECT_EQ(waiting.wait_for(20ms), std::future_status::timeout);
  ASSERT_TRUE(queue.pop(0ms));
  ASSERT_EQ(waiting.wait_for(500ms), std::future_status::ready);
  EXPECT_TRUE(waiting.get());
  ASSERT_TRUE(queue.try_raise(2));
  auto discard = std::async(std::launch::async, [&] {
    return queue.wait_space({});
  });
  queue.discard_if([](int) {
    return true;
  });
  ASSERT_EQ(discard.wait_for(500ms), std::future_status::ready);
  EXPECT_TRUE(discard.get());
  ASSERT_TRUE(queue.try_raise(3));
  std::stop_source stop;
  auto cancelled = std::async(std::launch::async, [&] {
    return queue.wait_space(stop.get_token());
  });
  stop.request_stop();
  ASSERT_EQ(cancelled.wait_for(500ms), std::future_status::ready);
  EXPECT_FALSE(cancelled.get());
}

TEST(StreamHubQueueTest, ExternalReadinessIncludesExistingDataAndOverflowWake) {
  safe::queue_t<int> queue(1);
  ASSERT_TRUE(queue.try_raise(1));
  unsigned notifications = 0;
  queue.set_notify([&] {
    ++notifications;
  });
  EXPECT_EQ(notifications, 1u);
  queue.wake();
  EXPECT_EQ(notifications, 2u);
  queue.pop(std::chrono::milliseconds(0));
  ASSERT_TRUE(queue.try_raise(2));
  EXPECT_EQ(notifications, 3u);
  queue.stop();
  EXPECT_EQ(notifications, 4u);
}

TEST(StreamHubQueueTest, ProducerDeadlineStopAndLocalInterrupt) {
  using namespace std::chrono_literals;
  safe::queue_t<int> queue(1);
  ASSERT_TRUE(queue.try_raise(1));
  EXPECT_FALSE(queue.wait_space({}, std::chrono::steady_clock::now() + 10ms));
  std::atomic_bool interrupt {false};
  auto waiting = std::async(std::launch::async, [&] {
    return queue.wait_space({}, std::chrono::steady_clock::now() + 2s, [&] {
      return interrupt.load();
    });
  });
  EXPECT_EQ(waiting.wait_for(20ms), std::future_status::timeout);
  interrupt.store(true);
  queue.wake_waiters();
  ASSERT_EQ(waiting.wait_for(500ms), std::future_status::ready);
  EXPECT_FALSE(waiting.get());
  auto stopped = std::async(std::launch::async, [&] {
    return queue.wait_space({}, std::chrono::steady_clock::now() + 2s);
  });
  queue.stop();
  ASSERT_EQ(stopped.wait_for(500ms), std::future_status::ready);
  EXPECT_FALSE(stopped.get());
}
