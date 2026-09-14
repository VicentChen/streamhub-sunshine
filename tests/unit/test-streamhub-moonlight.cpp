/**
 * @file tests/unit/test-streamhub-moonlight.cpp
 * @brief Exercise real ANNOUNCE responses and rollback with a bounded fake Provider.
 */
#ifdef __linux__
  #include "../streamhub-fixture.h"
  #include "../tests_common.h"
  #include "src/config.h"
  #include "src/process.h"
  #include "src/rtsp.h"
  #include "src/video.h"

  #include <boost/asio.hpp>
  #include <poll.h>
extern "C" {
  #include <moonlight-common-c/src/Rtsp.h>
}

namespace rtsp_stream {
  class rtsp_server_t;
  extern rtsp_server_t server;
  void free_msg(PRTSP_MESSAGE);
  using msg_t = util::safe_ptr<RTSP_MESSAGE, free_msg>;
  void cmd_announce(rtsp_server_t *, boost::asio::ip::tcp::socket &, launch_session_t &, msg_t &&);
}  // namespace rtsp_stream

namespace {
  using namespace streamhub_test;

  /** @brief Build complete RTSP requirements with selectable audio parameters. */
  std::string announce_payload(int channels = 2, int ms = 5, int quality = 0) {
    std::string payload = "s=integration\r\n";
    payload += "a=x-nv-audio.surround.numChannels:" + std::to_string(channels) + "\r\n";
    payload += "a=x-nv-audio.surround.channelMask:" + std::to_string(channels == 2 ? 3 : channels == 6 ? 0x3f :
                                                                                                         0x63f) +
               "\r\n";
    payload += "a=x-nv-audio.surround.AudioQuality:" + std::to_string(quality) + "\r\n";
    payload += "a=x-nv-aqos.packetDuration:" + std::to_string(ms) + "\r\n";
    for (auto [key, value] : std::initializer_list<std::pair<const char *, const char *>> {
           {"x-nv-video[0].packetSize", "1024"},
           {"x-nv-video[0].clientViewportHt", "1080"},
           {"x-nv-video[0].clientViewportWd", "1920"},
           {"x-nv-video[0].maxFPS", "60"},
           {"x-nv-vqos[0].bw.maximumBitrateKbps", "10000"},
           {"x-nv-video[0].videoEncoderSlicesPerFrame", "1"},
           {"x-nv-video[0].maxNumReferenceFrames", "1"},
           {"x-nv-video[0].encoderCscMode", "2"},
           {"x-nv-general.featureFlags", "0"}
         }) {
      payload += std::string("a=") + key + ":" + value + "\r\n";
    }
    return payload;
  }

  /** @brief Run ANNOUNCE against a fake Provider; no UDP peers or real shared resources are used. */
  void rejected_announce(int scenario) {
    auto old_socket = config::streamhub_socket;
    auto old_flags = video::codec_mode_flags;
    auto restore = util::fail_guard([&] {
      config::streamhub_socket = old_socket;
      video::codec_mode_flags = old_flags;
    });
    rtsp_stream::launch_session_t launch {};
    launch.id = 987;
    launch.input_id = "hdmi-main";
    launch.gcm_key.resize(16);
    launch.iv.resize(16);
    std::promise<void> requested;
    provider fake([&](int fd) {
      auto request = receive(fd);
      check(request.header.message_type == p::control_message_type::connect_request);
      requested.set_value();
      if (scenario == 0) {
        send(fd, message(p::control_message_type::connect_reject, p::result_info {p::result_code::busy, "test busy"}, request.header.request_id, 0));
      } else if (scenario == 1) {
        // Cancellation must interrupt waiting for ACCEPT, with no false success.
        char byte;
        EXPECT_LE(recv(fd, &byte, 1, 0), 0);
      } else {
        const auto &requirements = std::get<p::connect_request_info>(request.body);
        send(fd, message(p::control_message_type::connect_accept, accepted(requirements), request.header.request_id));
        auto resources = memory(requirements);
        // A regular sealed memfd is not a DMA-BUF: production validation must reject it.
        send(fd, message(p::control_message_type::resource, resources[4].info), resources[4].fd.get());
        auto stop = receive(fd);
        EXPECT_EQ(stop.header.message_type, p::control_message_type::stop_request);
        send(fd, message(p::control_message_type::stopped, p::result_info {p::result_code::ok, ""}, stop.header.request_id));
      }
    });
    config::streamhub_socket = fake.path;
    video::codec_mode_flags = SCM_H264;
    boost::asio::io_context io;
    using boost::asio::ip::tcp;
    tcp::acceptor listener(io, {boost::asio::ip::address_v4::loopback(), 0});
    tcp::socket client(io);
    client.connect(listener.local_endpoint());
    auto socket = listener.accept();
    auto payload = announce_payload();
    rtsp_stream::msg_t request(new RTSP_MESSAGE {});
    request->sequenceNumber = 7;
    request->payload = payload.data();
    request->payloadLength = payload.size();
    auto worker = std::async(std::launch::async, [&] {
      rtsp_stream::cmd_announce(&rtsp_stream::server, socket, launch, std::move(request));
      socket.close();
    });
    auto receipt = requested.get_future();
    ASSERT_EQ(receipt.wait_for(3s), std::future_status::ready);
    if (scenario == 1) {
      std::lock_guard guard(launch.lifecycle);
      launch.cancel.request_stop();
    }
    ASSERT_EQ(worker.wait_for(4s), std::future_status::ready);
    worker.get();
    std::string response;
    boost::system::error_code error;
    boost::asio::read(client, boost::asio::dynamic_buffer(response), error);
    EXPECT_EQ(response.find("RTSP/1.0 503"), 0);
    EXPECT_NE(response.find("CSeq: 7"), std::string::npos);
    EXPECT_EQ(rtsp_stream::session_count(), 0);
    EXPECT_EQ(proc::proc.running(), 0);
    fake.finish();
  }
}  // namespace

TEST(StreamHubMoonlightSessionTest, BusyRejectProduces503WithoutSession) {
  rejected_announce(0);
}

TEST(StreamHubMoonlightSessionTest, CancelInterruptsAcceptWait) {
  rejected_announce(1);
}

TEST(StreamHubMoonlightSessionTest, InvalidDmaStopsAcceptedSessionBefore503) {
  rejected_announce(2);
}

TEST(StreamHubMoonlightSessionTest, OversizedAudioIsRejectedBeforeProviderConnect) {
  using namespace streamhub_test;
  const auto old_socket = config::streamhub_socket;
  const auto old_flags = video::codec_mode_flags;
  char directory[] = "/tmp/sunshine-audio-limits-XXXXXX";
  ASSERT_NE(mkdtemp(directory), nullptr);
  auto cleanup = util::fail_guard([&] {
    config::streamhub_socket = old_socket;
    video::codec_mode_flags = old_flags;
    std::filesystem::remove_all(directory);
  });
  config::streamhub_socket = std::string(directory) + "/control.sock";
  video::codec_mode_flags = SCM_H264;
  unique_fd listener(socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC, 0));
  ASSERT_GE(listener.get(), 0);
  sockaddr_un address {};
  address.sun_family = AF_UNIX;
  std::strcpy(address.sun_path, config::streamhub_socket.c_str());
  ASSERT_EQ(bind(listener.get(), reinterpret_cast<sockaddr *>(&address), sizeof(address)), 0);
  ASSERT_EQ(listen(listener.get(), 4), 0);
  for (int channels : {6, 8}) {
    for (int ms : {10, 20}) {
      rtsp_stream::launch_session_t launch {};
      launch.id = 988;
      launch.input_id = "hdmi-main";
      launch.gcm_key.resize(16);
      launch.iv.resize(16);
      boost::asio::io_context io;
      using boost::asio::ip::tcp;
      tcp::acceptor acceptor(io, {boost::asio::ip::address_v4::loopback(), 0});
      tcp::socket client(io);
      client.connect(acceptor.local_endpoint());
      auto server_socket = acceptor.accept();
      auto payload = announce_payload(channels, ms, 1);
      rtsp_stream::msg_t request(new RTSP_MESSAGE {});
      request->sequenceNumber = 8;
      request->payload = payload.data();
      request->payloadLength = payload.size();
      rtsp_stream::cmd_announce(&rtsp_stream::server, server_socket, launch, std::move(request));
      server_socket.close();
      std::string response;
      boost::system::error_code error;
      boost::asio::read(client, boost::asio::dynamic_buffer(response), error);
      EXPECT_EQ(response.find("RTSP/1.0 503"), 0);
      pollfd pending {listener.get(), POLLIN, 0};
      EXPECT_EQ(poll(&pending, 1, 0), 0) << "Provider contacted for oversized audio";
      EXPECT_EQ(rtsp_stream::session_count(), 0);
    }
  }
}
#endif
