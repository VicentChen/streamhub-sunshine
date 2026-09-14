/**
 * @file tests/unit/test-streamhub-queue.cpp
 * @brief Verify bounded submission preserves ownership and per-session cleanup.
 */
#include "../tests_common.h"
#include "src/input.h"
#include "src/thread_safe.h"

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
