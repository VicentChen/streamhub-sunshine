/**
 * @file tests/unit/test-streamhub-media.cpp
 * @brief Video lease ownership, PCM timeline and controller bridge tests.
 */
#ifdef __linux__
  #include "src/streamhub/gamepad.h"
  #include "src/streamhub/media.h"
  #include "tests/streamhub-fixture.h"

  #include <future>
  #include <gtest/gtest.h>
  #include <limits>

namespace {
  using namespace streamhub_test;

  /** @brief Public resources with optional synchronization event capture. */
  struct fixture {
    p::connect_request_info request = streamhub::negotiate("x", {});  ///< Requested PCM layout.
    std::vector<resource> backing = memory(request);  ///< Fake peer mappings.
    std::shared_ptr<streamhub::resources> resources;  ///< Receiver mappings.

    /** @brief Build complete fake resources. */
    fixture(streamhub::resources::sync_fn sync = fake_sync):
        resources(std::make_shared<streamhub::resources>(request, sync)) {
      for (auto &item : backing) {
        resources->add(item.info, copy_fd(item.fd.get()));
      }
      resources->finish();
    }

    /** @brief Publish a minimal Annex-B fixture. */
    void video(uint64_t id, uint32_t slot, bool key, uint64_t pts, unsigned slices = 1) {
      std::vector<uint8_t> bytes = key ? std::vector<uint8_t> {0, 0, 1, 0x67, 0x80, 0, 0, 1, 0x68, 0x80, 0, 0, 1, 0x65, 0x80} : std::vector<uint8_t> {0, 0, 1, 0x41, 0x80};
      for (unsigned i = 1; i < slices; ++i) {
        bytes.insert(bytes.end(), {0, 0, 1, uint8_t(key ? 0x65 : 0x41), 0x80});
      }
      streamhub::mapped_resource mapping(copy_fd(backing[4 + slot].fd.get()), 4096, true);
      std::memcpy(mapping.data, bytes.data(), bytes.size());
      p::video_queue::producer producer(*static_cast<p::video_queue *>(resources->at(0)->data));
      producer.enqueue({id, pts, 0, slot, uint32_t(bytes.size()), key ? 1u : 0u, 0});
    }
  };

  TEST(StreamHubVideoTest, ConsumerReclaimsOnlyAfterDownstreamRead) {
    std::vector<bool> calls;
    fixture f([&](int, bool start) {
      calls.push_back(start);
    });
    calls.clear();
    streamhub::video_reader reader(f.resources, p::video_codec::h264);
    auto now = streamhub::monotonic_ns();
    f.video(10, 0, true, now);
    f.video(15, 1, false, now + 1000);
    auto frame = reader.next();
    ASSERT_TRUE(frame);
    EXPECT_EQ(frame->network_index, 1);
    EXPECT_EQ(frame->info.frame_id, 10);
    EXPECT_EQ(calls, std::vector<bool> {true});
    EXPECT_FALSE(reader.next());
    EXPECT_FALSE(reader.reclaim());
    frame->lease->complete();
    EXPECT_TRUE(reader.reclaim());
    EXPECT_EQ(calls, (std::vector<bool> {true, false}));
    auto second = reader.next();
    ASSERT_TRUE(second);
    EXPECT_EQ(second->network_index, 2);
    second.reset();
    EXPECT_TRUE(reader.reclaim());
    EXPECT_FALSE(reader.pending());
  }

  TEST(StreamHubVideoTest, WaitWakesForPublicationCompletionAndCancellation) {
    using namespace std::chrono_literals;
    fixture f;
    streamhub::video_reader reader(f.resources, p::video_codec::h264);
    auto limit = [] {
      return std::chrono::steady_clock::now() + 2s;
    };
    auto ready = std::async(std::launch::async, [&] {
      return reader.wait({}, limit());
    });
    EXPECT_EQ(ready.wait_for(20ms), std::future_status::timeout);
    f.video(0, 0, true, streamhub::monotonic_ns());
    ASSERT_EQ(ready.wait_for(500ms), std::future_status::ready);
    EXPECT_TRUE(ready.get());
    auto frame = reader.next();
    ASSERT_TRUE(frame);
    auto completion = std::async(std::launch::async, [&] {
      return reader.wait({}, limit());
    });
    EXPECT_EQ(completion.wait_for(20ms), std::future_status::timeout);
    frame->lease->complete();
    ASSERT_EQ(completion.wait_for(500ms), std::future_status::ready);
    EXPECT_TRUE(completion.get());
    EXPECT_TRUE(reader.reclaim());
    std::stop_source stop;
    auto cancelled = std::async(std::launch::async, [&] {
      return reader.wait(stop.get_token(), limit());
    });
    stop.request_stop();
    ASSERT_EQ(cancelled.wait_for(500ms), std::future_status::ready);
    EXPECT_FALSE(cancelled.get());
  }

  TEST(StreamHubVideoTest, LocalInterruptWakesEmptyReaderAndDeadlineRemainsBounded) {
    using namespace std::chrono_literals;
    fixture f;
    streamhub::video_reader reader(f.resources, p::video_codec::h264);
    std::atomic_bool interrupt {false};
    auto waiting = std::async(std::launch::async, [&] {
      return reader.wait({}, std::chrono::steady_clock::now() + 2s, [&] {
        return interrupt.load();
      });
    });
    EXPECT_EQ(waiting.wait_for(20ms), std::future_status::timeout);
    interrupt.store(true);
    p::video_queue::consumer notification(*static_cast<p::video_queue *>(f.resources->at(0)->data));
    notification.notification().signal();
    ASSERT_EQ(waiting.wait_for(500ms), std::future_status::ready);
    EXPECT_TRUE(waiting.get());
    EXPECT_FALSE(reader.next());
    EXPECT_FALSE(reader.wait({}, std::chrono::steady_clock::now() + 10ms));
  }

  TEST(StreamHubAudioTest, WaitWakesOnPcmPublicationAndCancellation) {
    using namespace std::chrono_literals;
    fixture f;
    streamhub::audio_reader reader(f.resources, f.request.audio);
    std::stop_source stop;
    auto waiting = std::async(std::launch::async, [&] {
      return reader.wait(stop.get_token());
    });
    EXPECT_EQ(waiting.wait_for(20ms), std::future_status::timeout);
    p::audio_queue::producer producer(*static_cast<p::audio_queue *>(f.resources->at(1)->data));
    producer.enqueue({0, streamhub::monotonic_ns(), 0, 0, 0});
    ASSERT_EQ(waiting.wait_for(500ms), std::future_status::ready);
    EXPECT_TRUE(waiting.get());
    ASSERT_TRUE(reader.next());
    auto cancelled = std::async(std::launch::async, [&] {
      return reader.wait(stop.get_token());
    });
    stop.request_stop();
    ASSERT_EQ(cancelled.wait_for(500ms), std::future_status::ready);
    EXPECT_FALSE(cancelled.get());
  }

  TEST(StreamHubVideoTest, AcceptedH264SliceCountIsNotHardwarePolicy) {
    fixture f;
    f.video(0, 0, true, streamhub::monotonic_ns(), 2);
    streamhub::video_reader reader(f.resources, p::video_codec::h264, 2);
    auto frame = reader.next();
    ASSERT_TRUE(frame);
    EXPECT_EQ(frame->bytes.size(), 20);
    EXPECT_FALSE(reader.reclaim());
    frame.reset();
    EXPECT_TRUE(reader.reclaim());
  }

  TEST(StreamHubVideoTest, FirstFrameFlagsAndSlotAreChecked) {
    fixture f;
    streamhub::video_reader reader(f.resources, p::video_codec::h264);
    f.video(0, 0, false, streamhub::monotonic_ns());
    EXPECT_THROW(reader.next(), std::runtime_error);
    fixture wrong;
    streamhub::video_reader wrong_reader(wrong.resources, p::video_codec::h264);
    wrong.video(0, 1, true, streamhub::monotonic_ns());
    EXPECT_THROW(wrong_reader.next(), std::runtime_error);
  }

  TEST(StreamHubVideoTest, HevcSlicesShareOneLeaseAndRejectCountOrPictureMismatch) {
    for (unsigned mode = 0; mode < 4; ++mode) {
      std::vector<bool> calls;
      fixture f([&](int, bool start) {
        calls.push_back(start);
      });
      calls.clear();
      const std::vector<uint8_t> bytes {0, 0, 1, 0x40, 1, 0x80, 0, 0, 1, 0x42, 1, 0x80, 0, 0, 1, 0x44, 1, 0x80, 0, 0, 1, 0x26, 1, 0xa0, 0, 0, 1, 0x26, 1, 0x20};
      streamhub::mapped_resource mapping(copy_fd(f.backing[4].fd.get()), 4096, true);
      std::memcpy(mapping.data, bytes.data(), bytes.size());
      if (mode == 2) {
        static_cast<uint8_t *>(mapping.data)[29] = 0xa0;
      }
      if (mode == 3) {
        static_cast<uint8_t *>(mapping.data)[28] = 2;
      }
      p::video_queue::producer producer(*static_cast<p::video_queue *>(f.resources->at(0)->data));
      producer.enqueue({0, streamhub::monotonic_ns(), 0, 0, uint32_t(bytes.size()), 1, 0});
      streamhub::video_reader reader(f.resources, p::video_codec::hevc, mode == 1 ? 1 : 2);
      if (mode) {
        EXPECT_THROW(reader.next(), std::runtime_error);
        EXPECT_EQ(calls, (std::vector<bool> {true, false}));
      } else {
        auto frame = reader.next();
        ASSERT_TRUE(frame);
        EXPECT_EQ(frame->bytes.size(), bytes.size());
        EXPECT_FALSE(reader.reclaim());
        frame.reset();
        EXPECT_TRUE(reader.reclaim());
        EXPECT_EQ(calls, (std::vector<bool> {true, false}));
      }
    }
  }

  TEST(StreamHubVideoTest, MalformedPayloadEndsDmaRead) {
    std::vector<bool> calls;
    fixture f([&](int, bool start) {
      calls.push_back(start);
    });
    calls.clear();
    f.video(0, 0, true, streamhub::monotonic_ns());
    {
      streamhub::mapped_resource mapping(copy_fd(f.backing[4].fd.get()), 4096, true);
      static_cast<uint8_t *>(mapping.data)[0] = 9;
    }
    streamhub::video_reader reader(f.resources, p::video_codec::h264);
    EXPECT_THROW(reader.next(), std::runtime_error);
    EXPECT_EQ(calls, (std::vector<bool> {true, false}));
  }

  TEST(StreamHubAudioTest, CopiesBeforeDequeueAndPreservesMissingBlockTime) {
    fixture f;
    streamhub::audio_reader reader(f.resources, f.request.audio);
    streamhub::mapped_resource pcm(copy_fd(f.backing[12].fd.get()), f.backing[12].info.byte_size, true);
    auto *values = static_cast<float *>(pcm.data);
    values[0] = 0.25f;
    values[1] = -0.5f;
    p::audio_queue::producer producer(*static_cast<p::audio_queue *>(f.resources->at(1)->data));
    auto now = streamhub::monotonic_ns();
    producer.enqueue({0, now, 0, 0, 0});
    auto first = reader.next();
    ASSERT_TRUE(first);
    values[0] = 0.9f;
    EXPECT_EQ(first->samples[0], 0.25f);
    EXPECT_EQ(first->samples[1], -0.5f);
    producer.enqueue({480, now + 10000000, 1, 1, 0});
    auto gap = reader.next();
    ASSERT_TRUE(gap);
    EXPECT_EQ(gap->sample_index, 480);
    EXPECT_TRUE(gap->discontinuity);
    producer.enqueue({720, now + 25000000, 2, 0, 0});
    EXPECT_THROW(reader.next(), std::runtime_error);
  }

  TEST(StreamHubAudioTest, RejectsNonFinitePcm) {
    fixture f;
    streamhub::mapped_resource pcm(copy_fd(f.backing[12].fd.get()), f.backing[12].info.byte_size, true);
    static_cast<float *>(pcm.data)[0] = std::numeric_limits<float>::quiet_NaN();
    p::audio_queue::producer producer(*static_cast<p::audio_queue *>(f.resources->at(1)->data));
    producer.enqueue({0, streamhub::monotonic_ns(), 0, 0, 0});
    streamhub::audio_reader reader(f.resources, f.request.audio);
    EXPECT_THROW(reader.next(), std::runtime_error);
  }

  TEST(StreamHubGamepadTest, BasicStateRumbleDisconnectAndFreshIdentity) {
    fixture f;
    streamhub::gamepad_bridge bridge(f.resources, f.request.gamepad);
    bridge.arrival(0, 0xffff, true);
    bridge.state(0, 1, {0x1001, 255, 0, -32768, -32768, 32767, 32767});
    bridge.flush();
    p::gamepad_input_queue::consumer events(*static_cast<p::gamepad_input_queue *>(f.resources->at(2)->data));
    ASSERT_FALSE(events.empty());
    auto first = events.front();
    events.dequeue();
    EXPECT_EQ(first.event_type, p::gamepad_event_type::connected);
    auto id = first.controller_id;
    auto state = events.front();
    events.dequeue();
    EXPECT_EQ(state.payload.state.buttons, p::gamepad_buttons::south | p::gamepad_buttons::dpad_up);
    EXPECT_EQ(state.payload.state.left_y, 32767);
    EXPECT_EQ(state.payload.state.right_y, -32767);
    EXPECT_EQ(state.payload.state.left_trigger, 65535);
    p::gamepad_feedback_queue::producer feedback(*static_cast<p::gamepad_feedback_queue *>(f.resources->at(3)->data));
    p::gamepad_feedback_info rumble {};
    rumble.feedback_type = p::gamepad_feedback_type::rumble;
    rumble.controller_id = id;
    rumble.event_time_ns = streamhub::monotonic_ns();
    rumble.payload.rumble = {123, 456, {}};
    feedback.enqueue(rumble);
    auto output = bridge.feedback();
    ASSERT_TRUE(output);
    EXPECT_EQ(output->index, 0);
    EXPECT_EQ(output->low, 123);
    bridge.state(0, 0, {});
    bridge.flush();
    EXPECT_EQ(events.front().event_type, p::gamepad_event_type::disconnected);
    events.dequeue();
    bridge.state(0, 1, {});
    bridge.flush();
    EXPECT_NE(events.front().controller_id, id);
    feedback.enqueue(rumble);
    EXPECT_FALSE(bridge.feedback());
  }

  TEST(StreamHubGamepadTest, DisabledServiceAndFullQueueStayBounded) {
    fixture f;
    streamhub::gamepad_bridge disabled(f.resources, {});
    disabled.state(0, 1, {});
    disabled.flush();
    p::gamepad_input_queue::consumer events(*static_cast<p::gamepad_input_queue *>(f.resources->at(2)->data));
    EXPECT_TRUE(events.empty());
    streamhub::gamepad_bridge bridge(f.resources, f.request.gamepad);
    for (int i = 0; i < 63; ++i) {
      bridge.state(0, 1, {});
    }
    bridge.flush();  // Exactly 64 events occupy the ring.
    bridge.state(0, 1, {});
    bridge.flush();
    EXPECT_TRUE(bridge.ready());
    // No blocking wait: cancellation can stop this worker between any two calls.
    EXPECT_FALSE(events.empty());
  }
}  // namespace
#endif
