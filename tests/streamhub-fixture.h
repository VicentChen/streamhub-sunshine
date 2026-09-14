/**
 * @file tests/streamhub-fixture.h
 * @brief Bounded fake Provider with real Unix sockets and sealed memory.
 */
#pragma once
#ifdef __linux__
  #include "src/streamhub/receiver.h"

  #include <array>
  #include <cstring>
  #include <fcntl.h>
  #include <filesystem>
  #include <future>
  #include <sys/mman.h>
  #include <sys/socket.h>
  #include <sys/un.h>
  #include <unistd.h>

namespace streamhub_test {
  namespace p = streamhub::protocal;
  using streamhub::unique_fd;
  using namespace std::chrono_literals;

  /** @brief Throw on failed fixture setup. */
  inline void check(bool value) {
    if (!value) {
      throw std::runtime_error("fake Provider setup failed");
    }
  }

  /** @brief Build a protocol message. */
  inline p::control_message message(p::control_message_type t, p::control_body b = {}, uint64_t req = 0, uint64_t sid = 7) {
    return {{p::protocol_major, p::protocol_minor, t, 0, 0, req, sid}, std::move(b)};
  }

  /** @brief Send one fake Provider packet. */
  inline void send(int fd, const p::control_message &value, int resource = -1) {
    std::vector<std::byte> data;
    check(p::encode_control(value, data) == p::control_error::none);
    iovec io {data.data(), data.size()};
    alignas(cmsghdr) std::array<std::byte, CMSG_SPACE(sizeof(int))> control {};
    msghdr header {};
    header.msg_iov = &io;
    header.msg_iovlen = 1;
    if (resource >= 0) {
      header.msg_control = control.data();
      header.msg_controllen = control.size();
      auto *c = CMSG_FIRSTHDR(&header);
      c->cmsg_level = SOL_SOCKET;
      c->cmsg_type = SCM_RIGHTS;
      c->cmsg_len = CMSG_LEN(sizeof(int));
      std::memcpy(CMSG_DATA(c), &resource, sizeof(resource));
    }
    check(sendmsg(fd, &header, MSG_NOSIGNAL) == ssize_t(data.size()));
  }

  /** @brief Receive a bounded Receiver request (which never has attached fds). */
  inline p::control_message receive(int fd) {
    std::array<std::byte, p::max_control_bytes> data;
    auto n = recv(fd, data.data(), data.size(), 0);
    check(n > 0);
    p::control_message result;
    check(p::decode_control(std::span(data.data(), size_t(n)), result) == p::control_error::none);
    return result;
  }

  /** @brief Sealed memory describing one public resource; DMA is simulated only through injected sync. */
  struct resource {
    p::resource_info info;  ///< Wire description.
    unique_fd fd;  ///< Fixture-owned backing.
  };

  /** @brief Create a complete empty public layout without hardware access. */
  inline std::vector<resource> memory(const p::connect_request_info &request) {
    std::vector<resource> result;
    auto add = [&](p::resource_info info, auto initialize) {
      unique_fd fd(memfd_create("sunshine-test", MFD_CLOEXEC | MFD_ALLOW_SEALING));
      check(fd.get() >= 0);
      check(ftruncate(fd.get(), info.byte_size) == 0);
      void *data = mmap(nullptr, info.byte_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd.get(), 0);
      check(data != MAP_FAILED);
      initialize(data);
      munmap(data, info.byte_size);
      check(fcntl(fd.get(), F_ADD_SEALS, F_SEAL_GROW | F_SEAL_SHRINK) == 0);
      result.push_back({info, std::move(fd)});
    };
    add({p::resource_kind::video_queue, sizeof(p::video_queue), 0}, [](void *v) {
      std::construct_at(static_cast<p::video_queue *>(v));
    });
    add({p::resource_kind::audio_queue, sizeof(p::audio_queue), 0}, [](void *v) {
      std::construct_at(static_cast<p::audio_queue *>(v));
    });
    add({p::resource_kind::gamepad_input_queue, sizeof(p::gamepad_input_queue), 0}, [](void *v) {
      std::construct_at(static_cast<p::gamepad_input_queue *>(v));
    });
    add({p::resource_kind::gamepad_feedback_queue, sizeof(p::gamepad_feedback_queue), 0}, [](void *v) {
      std::construct_at(static_cast<p::gamepad_feedback_queue *>(v));
    });
    for (uint32_t i = 0; i < 8; ++i) {
      add({p::resource_kind::video_buffer, 4096, 4096, p::memory_kind::dma_buf, i}, [](void *) {
      });
    }
    uint32_t channels = request.audio.format.channel_layout == p::audio_channel_layout::stereo ? 2 : request.audio.format.channel_layout == p::audio_channel_layout::surround_5_1 ? 6 :
                                                                                                                                                                                    8;
    auto bytes = request.audio.block_frames * channels * (request.audio.format.sample_format == p::audio_sample_format::f32_le ? 4 : 2);
    add({p::resource_kind::audio_pool, uint64_t(bytes) * p::audio_capacity, bytes}, [](void *) {
    });
    return result;
  }

  /** @brief Construct an exact supported ACCEPT. */
  inline p::connect_accept_info accepted(const p::connect_request_info &r) {
    return {{r.video.format, r.video.bitrate_bps, uint16_t(r.video.format.codec == p::video_codec::h264 ? 42 : 41), 1, r.video.slices_per_frame}, r.audio, r.gamepad, r.queue_layout};
  }

  /** @brief Temporary socket server whose worker propagates exceptions through finish(). */
  class provider {
  public:
    /** @brief Bind an isolated socket and start one bounded test exchange. */
    explicit provider(std::function<void(int)> work) {
      char name[] = "/tmp/sunshine-session-XXXXXX";
      auto *d = mkdtemp(name);
      check(d);
      directory = d;
      path = directory + "/control.sock";
      listener = unique_fd(socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0));
      check(listener.get() >= 0);
      sockaddr_un address {};
      address.sun_family = AF_UNIX;
      std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
      check(bind(listener.get(), reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0);
      check(listen(listener.get(), 8) == 0);
      worker = std::async(std::launch::async, [this, work = std::move(work)] {
        unique_fd client(accept4(listener.get(), nullptr, nullptr, SOCK_CLOEXEC));
        check(client.get() >= 0);
        timeval timeout {3, 0};
        check(setsockopt(client.get(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0);
        check(setsockopt(client.get(), SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == 0);
        work(client.get());
      });
    }

    /** @brief Wake an unaccepted worker and clean only fixture files. */
    ~provider() {
      shutdown(listener.get(), SHUT_RDWR);
      if (worker.valid()) {
        worker.wait();
      }
      std::filesystem::remove_all(directory);
    }

    /** @brief Join the fake peer and surface errors. */
    void finish() {
      worker.get();
    }

    std::string directory, path;  ///< Isolated paths.

  private:
    unique_fd listener;  ///< Listening socket.
    std::future<void> worker;  ///< One fake control exchange.
  };

  /** @brief Duplicate ownership for resource validation without disturbing Provider fds. */
  inline unique_fd copy_fd(int fd) {
    return unique_fd(fcntl(fd, F_DUPFD_CLOEXEC, 0));
  }

  /** @brief Deliberately simulated DMA synchronization; never selected by production configuration. */
  inline void fake_sync(int, bool) {}
}  // namespace streamhub_test
#endif
