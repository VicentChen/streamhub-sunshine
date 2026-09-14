/**
 * @file src/streamhub/receiver.cpp
 * @brief Session state and Linux resource validation using only public protocol types.
 */
#include "receiver.h"

#include <atomic>
#include <cerrno>
#include <fcntl.h>
#include <linux/dma-buf.h>
#include <stdexcept>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <system_error>
#include <time.h>
#include <unistd.h>

namespace streamhub {
  using namespace std::chrono_literals;
  namespace p = protocal;

  namespace {
    /** @brief Reject a protocol or resource contract violation. */
    void require(bool value, const char *reason) {
      if (!value) {
        throw std::runtime_error(reason);
      }
    }

    /** @brief Construct a public control message. */
    p::control_message message(p::control_message_type type, p::control_body body, uint64_t request, uint64_t session) {
      return {{p::protocol_major, p::protocol_minor, type, 0, 0, request, session}, std::move(body)};
    }
  }  // namespace

  uint64_t monotonic_ns() {
    timespec t {};
    if (clock_gettime(CLOCK_MONOTONIC, &t)) {
      throw std::system_error(errno, std::generic_category(), "StreamHub clock");
    }
    return uint64_t(t.tv_sec) * 1000000000 + t.tv_nsec;
  }

  void dma_read_sync(int fd, bool start) {
    dma_buf_sync request {};
    request.flags = DMA_BUF_SYNC_READ | (start ? DMA_BUF_SYNC_START : DMA_BUF_SYNC_END);
    auto until = std::chrono::steady_clock::now() + 1s;
    for (;;) {
      if (ioctl(fd, DMA_BUF_IOCTL_SYNC, &request) == 0) {
        return;
      }
      if ((errno != EINTR && errno != EAGAIN) || std::chrono::steady_clock::now() >= until) {
        throw std::system_error(errno, std::generic_category(), "StreamHub DMA read sync");
      }
    }
  }

  mapped_resource::mapped_resource(unique_fd fd, size_t size, bool writable):
      descriptor(std::move(fd)),
      data(MAP_FAILED),
      bytes(size) {
    data = mmap(nullptr, bytes, PROT_READ | (writable ? PROT_WRITE : 0), MAP_SHARED, descriptor.get(), 0);
    if (data == MAP_FAILED) {
      throw std::system_error(errno, std::generic_category(), "StreamHub mmap");
    }
  }

  mapped_resource::~mapped_resource() {
    if (data != MAP_FAILED) {
      munmap(data, bytes);
    }
  }

  resources::resources(p::connect_request_info request, sync_fn sync):
      request_(std::move(request)),
      sync_(std::move(sync)) {}

  void resources::add(const p::resource_info &info, unique_fd fd) {
    std::vector<std::byte> wire;
    require(p::encode_control(message(p::control_message_type::resource, info, 0, 1), wire) == p::control_error::none, "StreamHub malformed resource description");
    auto video = info.resource_kind == p::resource_kind::video_buffer;
    auto index = video ? 4 + info.slot_index : info.resource_kind == p::resource_kind::audio_pool ? 12 :
                                                                                                    size_t(info.resource_kind) - 1;
    require(index < mappings_.size() && !mappings_[index], "StreamHub duplicate resource slot");
    require(info.byte_size <= request_.max_shared_bytes - total_ && info.byte_size <= SIZE_MAX, "StreamHub resource budget exceeded");
    struct stat identity {};
    require(fstat(fd.get(), &identity) == 0 && identity.st_size >= 0 && uint64_t(identity.st_size) == info.byte_size, "StreamHub resource size mismatch");
    auto key = std::pair<uint64_t, uint64_t> {identity.st_dev, identity.st_ino};
    for (size_t i = 0; i < mappings_.size(); ++i) {
      require(!mappings_[i] || identities_[i] != key, "StreamHub aliased resource backing");
    }
    auto flags = fcntl(fd.get(), F_GETFL);
    require(flags >= 0 && !(flags & O_PATH) && (flags & O_ACCMODE) != O_WRONLY, "StreamHub unreadable resource");
    if (index < 4) {
      require((flags & O_ACCMODE) == O_RDWR, "StreamHub queue requires read/write access");
    }
    if (!video) {
      auto seals = fcntl(fd.get(), F_GET_SEALS);
      require(S_ISREG(identity.st_mode) && seals >= 0 && (seals & (F_SEAL_GROW | F_SEAL_SHRINK)) == (F_SEAL_GROW | F_SEAL_SHRINK), "StreamHub shared resource must have fixed size");
    }
    if (index == 12) {
      auto &audio = request_.audio;
      uint64_t channels = audio.format.channel_layout == p::audio_channel_layout::stereo ? 2 : audio.format.channel_layout == p::audio_channel_layout::surround_5_1 ? 6 :
                                                                                                                                                                      8;
      uint64_t sample_bytes = audio.format.sample_format == p::audio_sample_format::f32_le ? 4 : 2;
      require(info.slot_bytes == uint64_t(audio.block_frames) * channels * sample_bytes, "StreamHub PCM slot length mismatch");
    }
    auto memory = std::make_shared<mapped_resource>(std::move(fd), size_t(info.byte_size), index < 4);
    if (video) {
      sync_(memory->descriptor.get(), true);
      sync_(memory->descriptor.get(), false);
    }
    mappings_[index] = std::move(memory);
    identities_[index] = key;
    total_ += info.byte_size;
  }

  size_t resources::count() const {
    size_t result = 0;
    for (const auto &item : mappings_) {
      if (item) {
        ++result;
      }
    }
    return result;
  }

  void resources::finish() {
    require(count() == mappings_.size(), "StreamHub incomplete resource set");
    for (size_t i = 5; i < 12; ++i) {
      require(mappings_[i]->bytes == mappings_[4]->bytes, "StreamHub unequal video slot capacities");
    }
    for (size_t i = 0; i < 4; ++i) {
      auto *bytes = static_cast<std::byte *>(mappings_[i]->data);
      auto *head = reinterpret_cast<std::atomic<uint64_t> *>(bytes);
      auto *tail = reinterpret_cast<std::atomic<uint64_t> *>(bytes + 64);
      require(head->load(std::memory_order_acquire) == 0 && tail->load(std::memory_order_acquire) == 0, "StreamHub queue published before CONNECTED");
    }
    // Both APIs must describe the same local CLOCK_MONOTONIC epoch.
    auto clock = monotonic_ns();
    auto steady = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    require(steady >= 0 && uint64_t(steady) >= clock && uint64_t(steady) - clock < 100000000, "StreamHub incompatible steady clock");
  }

  std::shared_ptr<mapped_resource> resources::at(size_t index) const {
    return mappings_.at(index);
  }

  void resources::sync(size_t slot, bool start) const {
    sync_(mappings_.at(4 + slot)->descriptor.get(), start);
  }

  uint64_t receiver::send(p::control_message_type type, p::control_body body, std::stop_token stop) {
    require(request_ != UINT64_MAX, "StreamHub request IDs exhausted");
    connection_.send(message(type, std::move(body), ++request_, session_), std::chrono::steady_clock::now() + 1s, stop);
    return request_;
  }

  receiver::receiver(const std::string &socket, const p::connect_request_info &request, std::stop_token token, resources::sync_fn sync, std::chrono::milliseconds timeout):
      connection_(socket, std::chrono::steady_clock::now() + timeout, token),
      resources_(std::make_shared<resources>(request, std::move(sync))) {
    const auto until = std::chrono::steady_clock::now() + timeout;
    try {
      auto connect_id = send(p::control_message_type::connect_request, request, token);
      auto reply = connection_.receive(until, token).message;
      require(reply.header.request_id == connect_id, "StreamHub CONNECT correlation mismatch");
      if (reply.header.message_type == p::control_message_type::connect_reject) {
        throw std::runtime_error("StreamHub CONNECT rejected: " + std::get<p::result_info>(reply.body).detail);
      }
      require(reply.header.message_type == p::control_message_type::connect_accept, "StreamHub expected CONNECT_ACCEPT");
      session_ = reply.header.session_id;
      accepted_ = std::get<p::connect_accept_info>(reply.body);
      validate_accept(request, accepted_);
      while (resources_->count() != 13) {
        auto packet = connection_.receive(until, token);
        const auto &m = packet.message;
        if (event(m)) {
          continue;
        }
        require(m.header.message_type == p::control_message_type::resource && m.header.session_id == session_ && !m.header.request_id, "StreamHub unexpected resource state or session");
        resources_->add(std::get<p::resource_info>(m.body), std::move(packet.resource));
      }
      resources_->finish();
      auto ready = send(p::control_message_type::ready, {}, token);
      for (;;) {
        auto m = connection_.receive(until, token).message;
        if (event(m)) {
          continue;
        }
        require(m.header.message_type == p::control_message_type::connected && m.header.session_id == session_ && m.header.request_id == ready, "StreamHub CONNECTED correlation mismatch");
        connected_ = true;
        break;
      }
    } catch (...) {
      try {
        stop();
      } catch (...) {}
      throw;
    }
  }

  receiver::~receiver() {
    try {
      stop();
    } catch (...) {}
  }

  bool receiver::event(const p::control_message &m) {
    if (m.header.message_type == p::control_message_type::input_list) {
      require(!m.header.session_id && !m.header.request_id, "StreamHub unexpected input list correlation");
      return true;
    }
    if (m.header.message_type != p::control_message_type::status) {
      return false;
    }
    require(m.header.session_id == session_ && !m.header.request_id, "StreamHub STATUS session mismatch");
    const auto &status = std::get<p::status_info>(m.body);
    if (status.state == p::stream_state::failed) {
      throw std::runtime_error("StreamHub session=" + std::to_string(session_) + " scope=" + std::to_string(unsigned(status.scope)) + " failed: " + status.reason.detail);
    }
    return true;
  }

  bool receiver::poll() {
    if (idr_request_ && std::chrono::steady_clock::now() >= idr_deadline_) {
      throw std::runtime_error("StreamHub IDR acknowledgement timed out");
    }
    // Bound work per tick so a noisy peer cannot starve media or cancellation.
    for (int i = 0; i < 16 && connection_.pending(); ++i) {
      auto m = connection_.receive(std::chrono::steady_clock::now() + 100ms).message;
      if (event(m)) {
        continue;
      }
      require(m.header.session_id == session_, "StreamHub control session mismatch");
      if (m.header.message_type == p::control_message_type::stopped && !m.header.request_id) {
        connected_ = false;
        session_ = 0;
        return false;
      }
      require(m.header.message_type == p::control_message_type::result && idr_request_ && m.header.request_id == idr_request_, "StreamHub unexpected control response");
      require(std::get<p::result_info>(m.body).code == p::result_code::ok, "StreamHub IDR request rejected");
      idr_request_ = 0;
    }
    return connected_;
  }

  void receiver::request_idr() {
    require(connected_, "StreamHub IDR outside connected session");
    if (!idr_request_) {
      idr_request_ = send(p::control_message_type::request_idr, {});
      idr_deadline_ = std::chrono::steady_clock::now() + 1s;
    }
  }

  void receiver::stop() {
    if (!session_) {
      return;
    }
    auto stopping = send(p::control_message_type::stop_request, p::result_info {p::result_code::ok, "Receiver stopped"});
    const auto until = std::chrono::steady_clock::now() + 1s;
    for (;;) {
      auto m = connection_.receive(until).message;
      if (event(m)) {
        continue;
      }
      require(m.header.session_id == session_, "StreamHub stop session mismatch");
      if (m.header.message_type == p::control_message_type::stopped) {
        require(m.header.request_id == stopping || !m.header.request_id, "StreamHub STOPPED correlation mismatch");
        connected_ = false;
        session_ = 0;
        return;
      }
      require(m.header.message_type == p::control_message_type::resource || (m.header.message_type == p::control_message_type::result && m.header.request_id == idr_request_), "StreamHub unexpected stop response");
    }
  }
}  // namespace streamhub
