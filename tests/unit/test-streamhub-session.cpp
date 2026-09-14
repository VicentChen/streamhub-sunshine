/**
 * @file tests/unit/test-streamhub-session.cpp
 * @brief Negotiation, directory, resource and session failure tests.
 */
#ifdef __linux__
  #include "src/streamhub/catalog.h"
  #include "tests/streamhub-fixture.h"

  #include <fstream>
  #include <gtest/gtest.h>
  #include <iomanip>

namespace {
  using namespace streamhub_test;

  TEST(StreamHubNegotiationTest, ExactRateBitrateAndConstraints) {
    streamhub::requirements c;
    auto r = streamhub::negotiate("hdmi-main", c);
    EXPECT_EQ(r.video.format.fps_num, 60);
    EXPECT_EQ(r.video.format.fps_den, 1);
    EXPECT_EQ(r.video.bitrate_bps, 10000000);
    c.fps_x100 = 5994;
    c.references = 1;
    r = streamhub::negotiate("hdmi-main", c);
    EXPECT_EQ(r.video.format.fps_num, 60000);
    EXPECT_EQ(r.video.format.fps_den, 1001);
    EXPECT_EQ(r.video.max_ref_frames, 1);
    c.codec = 1;
    c.csc = 3;
    r = streamhub::negotiate("hdmi-main", c);
    EXPECT_EQ(r.video.format.profile, p::video_profile::hevc_main);
    EXPECT_EQ(r.video.format.range, p::video_range::full);
  }

  TEST(StreamHubNegotiationTest, ChannelsAndBlockLength) {
    for (auto [channels, mask] : {std::pair {2, 3}, {6, 0x3f}, {8, 0x63f}}) {
      streamhub::requirements c;
      c.channels = channels;
      c.channel_mask = mask;
      for (int ms : {5, 10, 20}) {
        c.packet_ms = ms;
        auto r = streamhub::negotiate("x", c);
        EXPECT_EQ(r.audio.block_frames, ms * 48);
        EXPECT_EQ(r.audio.format.sample_rate, 48000);
      }
    }
  }

  TEST(StreamHubNegotiationTest, RejectsInvalidAndUnrepresentableRequirements) {
    std::vector<std::function<void(streamhub::requirements &)>> changes {
      [](auto &c) {
        c.codec = 2;
      },
      [](auto &c) {
        c.depth = 1;
      },
      [](auto &c) {
        c.chroma = 1;
      },
      [](auto &c) {
        c.intra_refresh = 1;
      },
      [](auto &c) {
        c.width = 0;
      },
      [](auto &c) {
        c.fps = 0;
      },
      [](auto &c) {
        c.bitrate_kbps = INT32_MAX;
      },
      [](auto &c) {
        c.references = -1;
      },
      [](auto &c) {
        c.slices = 0;
      },
      [](auto &c) {
        c.csc = 4;
      },
      [](auto &c) {
        c.channel_mask = 0;
      },
      [](auto &c) {
        c.channels = 3;
      },
      [](auto &c) {
        c.packet_ms = 7;
      }
    };
    for (const auto &change : changes) {
      streamhub::requirements c;
      change(c);
      EXPECT_THROW(streamhub::negotiate("x", c), std::invalid_argument);
    }
  }

  TEST(StreamHubNegotiationTest, AcceptMustMatchEveryRequiredField) {
    auto r = streamhub::negotiate("x", {});
    r.video.max_ref_frames = 1;
    r.video.max_level = 42;
    auto a = accepted(r);
    EXPECT_NO_THROW(streamhub::validate_accept(r, a));
    std::vector<std::function<void(p::connect_accept_info &)>> changes {
      [](auto &a) {
        ++a.video.format.width;
      },
      [](auto &a) {
        ++a.video.format.height;
      },
      [](auto &a) {
        ++a.video.format.fps_num;
      },
      [](auto &a) {
        ++a.video.format.fps_den;
      },
      [](auto &a) {
        a.video.format.codec = p::video_codec::hevc;
      },
      [](auto &a) {
        a.video.format.profile = p::video_profile::hevc_main;
      },
      [](auto &a) {
        a.video.format.color = p::video_color::bt601_525_sdr;
      },
      [](auto &a) {
        a.video.format.range = p::video_range::full;
      },
      [](auto &a) {
        ++a.video.bitrate_bps;
      },
      [](auto &a) {
        ++a.video.slices_per_frame;
      },
      [](auto &a) {
        ++a.video.ref_frames;
      },
      [](auto &a) {
        ++a.video.level;
      },
      [](auto &a) {
        ++a.audio.block_frames;
      },
      [](auto &a) {
        ++a.audio.format.sample_rate;
      },
      [](auto &a) {
        a.audio.format.sample_format = p::audio_sample_format::s16_le;
      },
      [](auto &a) {
        a.audio.format.channel_layout = p::audio_channel_layout::surround_5_1;
      },
      [](auto &a) {
        ++a.queue_layout;
      },
      [](auto &a) {
        a.gamepad.input_features |= 2;
      },
      [](auto &a) {
        a.gamepad.feedback_features |= 2;
      }
    };
    for (const auto &change : changes) {
      auto bad = a;
      change(bad);
      EXPECT_THROW(streamhub::validate_accept(r, bad), std::invalid_argument);
    }
    a.gamepad = {};
    EXPECT_NO_THROW(streamhub::validate_accept(r, a));
  }

  TEST(StreamHubInputCatalogTest, ReorderRenameRemovalOfflineAndRestore) {
    streamhub::catalog directory;
    p::input_info a {"a", "First", p::input_state::unavailable, p::input_state::unavailable};
    p::input_info b {"b", "Second", p::input_state::available, p::input_state::available};
    directory.update({{a, b}});
    auto entries = directory.entries();
    auto id = entries[0].app_id;
    EXPECT_EQ(directory.select(id), "a");
    a.name = "Renamed";
    directory.update({{b, a}});
    EXPECT_EQ(directory.entries()[1].app_id, id);
    directory.update({{b}});
    EXPECT_THROW(directory.select(id), std::runtime_error);
    directory.update({{a}});
    EXPECT_EQ(directory.select(id), "a");
    directory.offline();
    EXPECT_FALSE(directory.online());
    EXPECT_THROW(directory.select(id), std::runtime_error);
    directory.update({{a}});
    EXPECT_EQ(directory.entries()[0].app_id, id);
  }

  TEST(StreamHubInputCatalogTest, PersistentCollisionResolution) {
    char name[] = "/tmp/sunshine-identities-XXXXXX";
    auto *d = mkdtemp(name);
    ASSERT_NE(d, nullptr);
    auto path = std::string(d) + "/ids";
    uint32_t hash = (2166136261u ^ 'x') * 16777619u;
    auto collision = hash & 0x7fffffff;
    {
      std::ofstream out(path);
      out << std::quoted("reserved") << ' ' << collision << '\n';
    }
    int id;
    {
      streamhub::catalog c(path);
      c.update({{{"x", "X", p::input_state::available, p::input_state::available}}});
      id = c.entries()[0].app_id;
      EXPECT_NE(uint32_t(id), collision);
    }
    {
      streamhub::catalog c(path);
      c.update({{{"x", "New name", p::input_state::unavailable, p::input_state::unavailable}}});
      EXPECT_EQ(c.entries()[0].app_id, id);
    }
    std::filesystem::remove_all(d);
  }

  TEST(StreamHubResourceTest, CompleteOutOfOrderAndDuplicateIdentity) {
    auto r = streamhub::negotiate("x", {});
    auto data = memory(r);
    streamhub::resources resources(r, fake_sync);
    for (auto i = data.rbegin(); i != data.rend(); ++i) {
      resources.add(i->info, copy_fd(i->fd.get()));
    }
    EXPECT_NO_THROW(resources.finish());
    EXPECT_EQ(resources.count(), 13);
    EXPECT_THROW(resources.add(data[0].info, copy_fd(data[0].fd.get())), std::runtime_error);
    streamhub::resources alias(r, fake_sync);
    alias.add(data[4].info, copy_fd(data[4].fd.get()));
    EXPECT_THROW(alias.add(data[5].info, copy_fd(data[4].fd.get())), std::runtime_error);
  }

  TEST(StreamHubResourceTest, SizeBudgetMemoryKindAndMissingSlots) {
    auto r = streamhub::negotiate("x", {});
    auto data = memory(r);
    streamhub::resources resources(r, fake_sync);
    EXPECT_THROW(resources.finish(), std::runtime_error);
    EXPECT_THROW(resources.add(data[0].info, copy_fd(data[1].fd.get())), std::runtime_error);
    auto info = data[4].info;
    info.memory_kind = p::memory_kind::shared_memory;
    EXPECT_THROW(resources.add(info, copy_fd(data[4].fd.get())), std::runtime_error);
    r.max_shared_bytes = 1;
    streamhub::resources small(r, fake_sync);
    EXPECT_THROW(small.add(data[0].info, copy_fd(data[0].fd.get())), std::runtime_error);
    streamhub::resources real(streamhub::negotiate("x", {}));
    EXPECT_THROW(real.add(data[4].info, copy_fd(data[4].fd.get())), std::system_error);
  }

  TEST(StreamHubResourceTest, RejectsUnsealedMemoryAndPublishedQueue) {
    auto r = streamhub::negotiate("x", {});
    auto data = memory(r);
    unique_fd fd(memfd_create("unsealed", MFD_CLOEXEC | MFD_ALLOW_SEALING));
    ASSERT_EQ(ftruncate(fd.get(), sizeof(p::video_queue)), 0);
    streamhub::resources resources(r, fake_sync);
    EXPECT_THROW(resources.add(data[0].info, copy_fd(fd.get())), std::runtime_error);
    streamhub::mapped_resource map(copy_fd(data[0].fd.get()), sizeof(p::video_queue), true);
    p::video_queue::producer producer(*static_cast<p::video_queue *>(map.data));
    producer.enqueue({});
    for (auto &item : data) {
      resources.add(item.info, copy_fd(item.fd.get()));
    }
    EXPECT_THROW(resources.finish(), std::runtime_error);
  }

  TEST(StreamHubSessionTest, HandshakeIdrAndStop) {
    provider fake([](int fd) {
      auto connect = receive(fd);
      auto r = std::get<p::connect_request_info>(connect.body);
      send(fd, message(p::control_message_type::connect_accept, accepted(r), connect.header.request_id));
      auto data = memory(r);
      for (auto i = data.rbegin(); i != data.rend(); ++i) {
        send(fd, message(p::control_message_type::resource, i->info), i->fd.get());
      }
      auto ready = receive(fd);
      check(ready.header.message_type == p::control_message_type::ready);
      send(fd, message(p::control_message_type::connected, {}, ready.header.request_id));
      auto idr = receive(fd);
      check(idr.header.message_type == p::control_message_type::request_idr);
      send(fd, message(p::control_message_type::result, p::result_info {p::result_code::ok, ""}, idr.header.request_id));
      auto stop = receive(fd);
      check(stop.header.message_type == p::control_message_type::stop_request);
      send(fd, message(p::control_message_type::stopped, p::result_info {p::result_code::ok, ""}, stop.header.request_id));
    });
    streamhub::receiver client(fake.path, streamhub::negotiate("x", {}), {}, fake_sync);
    EXPECT_EQ(client.id(), 7);
    client.request_idr();
    std::this_thread::sleep_for(10ms);
    EXPECT_TRUE(client.poll());
    client.stop();
    EXPECT_EQ(client.id(), 0);
    fake.finish();
  }

  TEST(StreamHubSessionTest, RejectionAndEofAreExplicit) {
    for (int mode = 0; mode < 2; ++mode) {
      provider fake([mode](int fd) {
        auto r = receive(fd);
        if (!mode) {
          send(fd, message(p::control_message_type::connect_reject, p::result_info {p::result_code::busy, "busy"}, r.header.request_id, 0));
        }
      });
      EXPECT_THROW(streamhub::receiver(fake.path, streamhub::negotiate("x", {}), {}, fake_sync, 200ms), std::runtime_error);
      fake.finish();
    }
  }

  TEST(StreamHubSessionTest, ResourceFailureAndCancellationSendStop) {
    for (int mode = 0; mode < 4; ++mode) {
      provider fake([mode](int fd) {
        auto c = receive(fd);
        auto r = std::get<p::connect_request_info>(c.body);
        send(fd, message(p::control_message_type::connect_accept, accepted(r), c.header.request_id));
        auto data = memory(r);
        if (mode == 0 || mode == 1) {
          send(fd, message(p::control_message_type::resource, data[0].info, 0, mode == 1 ? 99 : 7), data[0].fd.get());
          if (!mode) {
            send(fd, message(p::control_message_type::resource, data[0].info), data[0].fd.get());
          }
        }
        auto stop = receive(fd);
        check(stop.header.message_type == p::control_message_type::stop_request);
        send(fd, message(p::control_message_type::stopped, p::result_info {p::result_code::ok, ""}, stop.header.request_id));
      });
      std::stop_source source;
      std::jthread cancel([&] {
        if (mode == 3) {
          std::this_thread::sleep_for(30ms);
          source.request_stop();
        }
      });
      EXPECT_THROW(streamhub::receiver(fake.path, streamhub::negotiate("x", {}), source.get_token(), fake_sync, 100ms), std::runtime_error);
      fake.finish();
    }
  }
}  // namespace
#endif
