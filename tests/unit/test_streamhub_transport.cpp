/**
 * @file tests/unit/test_streamhub_transport.cpp
 * @brief Isolated Linux control transport and descriptor lifetime tests.
 */
#ifdef __linux__
  #include "src/config.h"
  #include "src/streamhub/transport.h"

  #include <array>
  #include <cstring>
  #include <fcntl.h>
  #include <filesystem>
  #include <fstream>
  #include <future>
  #include <gtest/gtest.h>
  #include <sys/socket.h>
  #include <sys/un.h>
  #include <sys/wait.h>
  #include <unistd.h>

namespace {
  namespace p = streamhub::protocal;
  using namespace std::chrono_literals;
  using streamhub::unique_fd;

  /** @brief Build a connection-level GET_INPUTS request. */
  p::control_message query() {
    return {{p::protocol_major, p::protocol_minor, p::control_message_type::get_inputs, 0, 0, 1, 0}, std::monostate {}};
  }

  /** @brief Count live descriptors for leak checks. */
  size_t fd_count() {
    return std::distance(std::filesystem::directory_iterator("/proc/self/fd"), std::filesystem::directory_iterator {});
  }

  /** @brief Filesystem SEQPACKET Provider fixture, isolated from running services. */
  class StreamHubTransportTest: public testing::Test {
  protected:
    std::string directory;  ///< Private temporary directory.
    std::string path;  ///< Fixture socket.
    unique_fd listener;  ///< Listening socket.

    /** @brief Bind an isolated Provider endpoint. */
    void SetUp() override {
      std::string temp = (std::filesystem::temp_directory_path() / "t-XXXXXX").string();
      auto *result = mkdtemp(temp.data());
      ASSERT_NE(result, nullptr);
      directory = result;
      path = directory + "/control.sock";
      listener = unique_fd(socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0));
      ASSERT_GE(listener.get(), 0);
      sockaddr_un address {};
      address.sun_family = AF_UNIX;
      ASSERT_LT(path.size(), sizeof(address.sun_path));
      std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
      ASSERT_EQ(bind(listener.get(), reinterpret_cast<sockaddr *>(&address), sizeof(address)), 0);
      ASSERT_EQ(listen(listener.get(), 8), 0);
    }

    /** @brief Remove only fixture-owned temporary files. */
    void TearDown() override {
      std::filesystem::remove_all(directory);
    }

    /** @brief Accept the connected Receiver. */
    unique_fd accept_peer() {
      return unique_fd(accept4(listener.get(), nullptr, nullptr, SOCK_CLOEXEC));
    }

    /** @brief Send raw wire bytes and optional descriptors to exercise rejection paths. */
    void raw(int peer, const std::vector<std::byte> &bytes, const std::vector<int> &fds = {}) {
      iovec io {const_cast<std::byte *>(bytes.data()), bytes.size()};
      alignas(cmsghdr) std::array<std::byte, CMSG_SPACE(253 * sizeof(int))> control {};
      msghdr message {};
      message.msg_iov = &io;
      message.msg_iovlen = 1;
      if (!fds.empty()) {
        message.msg_control = control.data();
        message.msg_controllen = CMSG_SPACE(fds.size() * sizeof(int));
        auto *header = CMSG_FIRSTHDR(&message);
        header->cmsg_level = SOL_SOCKET;
        header->cmsg_type = SCM_RIGHTS;
        header->cmsg_len = CMSG_LEN(fds.size() * sizeof(int));
        std::memcpy(CMSG_DATA(header), fds.data(), fds.size() * sizeof(int));
      }
      ASSERT_EQ(sendmsg(peer, &message, MSG_NOSIGNAL), static_cast<ssize_t>(bytes.size()));
    }

    /** @brief Encode a valid request for malformed-packet mutations. */
    std::vector<std::byte> bytes() {
      std::vector<std::byte> result;
      EXPECT_EQ(p::encode_control(query(), result), p::control_error::none);
      return result;
    }
  };

  TEST_F(StreamHubTransportTest, CrossProcessInputList) {
    pid_t child = fork();
    ASSERT_GE(child, 0);
    if (child == 0) {
      alarm(5);
      auto peer = accept_peer();
      std::array<std::byte, p::max_control_bytes> data;
      auto count = recv(peer.get(), data.data(), data.size(), 0);
      p::control_message request;
      if (count <= 0 || p::decode_control(std::span(data.data(), count), request) != p::control_error::none || request.header.message_type != p::control_message_type::get_inputs) {
        _exit(1);
      }
      p::control_message reply {{p::protocol_major, p::protocol_minor, p::control_message_type::input_list, 0, 0, request.header.request_id, 0}, p::input_list_info {{{"hdmi-main", "HDMI", p::input_state::available, p::input_state::unavailable}}}};
      std::vector<std::byte> encoded;
      if (p::encode_control(reply, encoded) != p::control_error::none || send(peer.get(), encoded.data(), encoded.size(), MSG_NOSIGNAL) != static_cast<ssize_t>(encoded.size())) {
        _exit(2);
      }
      _exit(0);
    }
    {
      streamhub::transport client(path, std::chrono::steady_clock::now() + 2s);
      client.send(query(), std::chrono::steady_clock::now() + 2s);
      auto packet = client.receive(std::chrono::steady_clock::now() + 2s);
      EXPECT_EQ(packet.message.header.request_id, 1);
      EXPECT_EQ(std::get<p::input_list_info>(packet.message.body).inputs.at(0).input_id, "hdmi-main");
      EXPECT_EQ(packet.resource.get(), -1);
    }
    int status = 0;
    ASSERT_EQ(waitpid(child, &status, 0), child);
    ASSERT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 0);
  }

  TEST_F(StreamHubTransportTest, RejectsMalformedPacketsAndClosesAllRights) {
    for (int mode = 0; mode < 5; ++mode) {
      streamhub::transport client(path, std::chrono::steady_clock::now() + 1s);
      auto peer = accept_peer();
      unique_fd fd(open("/dev/null", O_RDONLY | O_CLOEXEC));
      auto baseline = fd_count();
      auto payload = bytes();
      if (mode == 0) {
        payload[0] = std::byte {99};  // Unsupported major version.
      } else if (mode == 1) {
        payload.resize(12);  // Truncated header.
      } else if (mode == 2) {
        payload.resize(p::max_control_bytes + 1);  // MSG_TRUNC.
      } else if (mode == 3) {
        payload.back() = std::byte {255};  // Invalid session shape.
      }
      raw(peer.get(), payload, std::vector<int>(253, fd.get()));
      EXPECT_THROW(client.receive(std::chrono::steady_clock::now() + 1s), std::runtime_error);
      EXPECT_EQ(fd_count(), baseline);
    }
  }

  TEST_F(StreamHubTransportTest, ResourceRightsAndCloseOnExec) {
    streamhub::transport client(path, std::chrono::steady_clock::now() + 1s);
    auto peer = accept_peer();
    unique_fd fd(open("/dev/null", O_RDONLY | O_CLOEXEC));
    p::control_message resource {{p::protocol_major, p::protocol_minor, p::control_message_type::resource, 0, 0, 0, 2}, p::resource_info {p::resource_kind::audio_pool, 512 * p::audio_capacity, 512, p::memory_kind::shared_memory, 0}};
    std::vector<std::byte> encoded;
    ASSERT_EQ(p::encode_control(resource, encoded), p::control_error::none);
    auto baseline = fd_count();
    raw(peer.get(), encoded, {fd.get()});
    {
      auto packet = client.receive(std::chrono::steady_clock::now() + 1s);
      ASSERT_GE(packet.resource.get(), 0);
      EXPECT_NE(fcntl(packet.resource.get(), F_GETFD) & FD_CLOEXEC, 0);
      unique_fd moved(std::move(packet.resource));
      EXPECT_EQ(packet.resource.get(), -1);
      unique_fd destination;
      destination = std::move(moved);
      EXPECT_EQ(moved.get(), -1);
      EXPECT_GE(destination.get(), 0);
    }
    EXPECT_EQ(fd_count(), baseline);
    raw(peer.get(), encoded);
    EXPECT_THROW(client.receive(std::chrono::steady_clock::now() + 1s), std::runtime_error);
    raw(peer.get(), encoded, {fd.get(), fd.get()});
    EXPECT_THROW(client.receive(std::chrono::steady_clock::now() + 1s), std::runtime_error);
    EXPECT_EQ(fd_count(), baseline);
    client.send(resource, std::chrono::steady_clock::now() + 1s, {}, fd.get());
    // Drain the sent resource with recvmsg so the test owns and closes its received fd.
    std::array<std::byte, 1024> data {};
    alignas(cmsghdr) std::array<std::byte, CMSG_SPACE(sizeof(int))> rights {};
    iovec io {data.data(), data.size()};
    msghdr message {};
    message.msg_iov = &io;
    message.msg_iovlen = 1;
    message.msg_control = rights.data();
    message.msg_controllen = rights.size();
    ASSERT_GT(recvmsg(peer.get(), &message, MSG_CMSG_CLOEXEC), 0);
    ASSERT_NE(CMSG_FIRSTHDR(&message), nullptr);
    int received;
    std::memcpy(&received, CMSG_DATA(CMSG_FIRSTHDR(&message)), sizeof(received));
    unique_fd owned(received);
  }

  TEST_F(StreamHubTransportTest, EofTimeoutAndReceiveCancellation) {
    streamhub::transport client(path, std::chrono::steady_clock::now() + 1s);
    auto peer = accept_peer();
    EXPECT_THROW(client.receive(std::chrono::steady_clock::now() + 20ms), std::runtime_error);
    std::stop_source stop;
    auto pending = std::async(std::launch::async, [&] {
      EXPECT_THROW(client.receive(std::chrono::steady_clock::now() + 2s, stop.get_token()), std::runtime_error);
    });
    std::this_thread::sleep_for(30ms);
    stop.request_stop();
    EXPECT_EQ(pending.wait_for(500ms), std::future_status::ready);
    pending.get();
    peer = unique_fd();
    EXPECT_THROW(client.receive(std::chrono::steady_clock::now() + 1s), std::runtime_error);
    EXPECT_THROW(client.send(query(), std::chrono::steady_clock::now() + 1s), std::system_error);
  }

  TEST_F(StreamHubTransportTest, SendBackpressureCanBeCancelled) {
    streamhub::transport client(path, std::chrono::steady_clock::now() + 1s);
    auto peer = accept_peer();
    std::stop_source stop;
    auto pending = std::async(std::launch::async, [&] {
      try {
        for (;;) {
          client.send(query(), std::chrono::steady_clock::now() + 2s, stop.get_token());
        }
      } catch (const std::runtime_error &error) {
        EXPECT_NE(std::string(error.what()).find("cancelled"), std::string::npos);
      }
    });
    std::this_thread::sleep_for(30ms);
    std::this_thread::sleep_for(30ms);
    stop.request_stop();
    EXPECT_EQ(pending.wait_for(500ms), std::future_status::ready);
    pending.get();
  }

  TEST_F(StreamHubTransportTest, FailurePathsDoNotLeak) {
    auto baseline = fd_count();
    for (int i = 0; i < 3; ++i) {
      EXPECT_THROW(streamhub::transport(path + "-missing", std::chrono::steady_clock::now() + 1s), std::system_error);
      EXPECT_THROW(streamhub::transport("", std::chrono::steady_clock::now() + 1s), std::invalid_argument);
      EXPECT_THROW(streamhub::transport(std::string(200, '/'), std::chrono::steady_clock::now() + 1s), std::invalid_argument);
      EXPECT_THROW(streamhub::transport(path + std::string("\0x", 2), std::chrono::steady_clock::now() + 1s), std::invalid_argument);
      std::stop_source stop;
      stop.request_stop();
      EXPECT_THROW(streamhub::transport(path, std::chrono::steady_clock::now() + 1s, stop.get_token()), std::runtime_error);
      EXPECT_EQ(fd_count(), baseline);
    }
    streamhub::transport client(path, std::chrono::steady_clock::now() + 1s);
    auto peer = accept_peer();
    EXPECT_THROW(client.send(query(), std::chrono::steady_clock::now() + 1s, {}, listener.get()), std::invalid_argument);
    auto invalid = query();
    invalid.header.major = 999;
    EXPECT_THROW(client.send(invalid, std::chrono::steady_clock::now() + 1s), std::invalid_argument);
  }

  #ifdef SUNSHINE_TESTS
  TEST_F(StreamHubTransportTest, SocketConfigurationIsExplicit) {
    auto original = config::streamhub_socket;
    auto original_apps = config::stream.file_apps;
    auto original_settings = config::modified_config_settings;
    auto apps = directory + "/apps.json";
    std::ofstream(apps) << "{\"apps\": []}";
    EXPECT_NO_THROW(config::apply_config_for_test("streamhub_socket = " + path + "\nfile_apps = " + apps + "\n"));
    EXPECT_EQ(config::streamhub_socket, path);
    config::streamhub_socket = original;
    config::stream.file_apps = original_apps;
    config::modified_config_settings = std::move(original_settings);
  }
  #endif
}  // namespace
#endif
