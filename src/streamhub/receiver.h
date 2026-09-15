/**
 * @file src/streamhub/receiver.h
 * @brief Public-protocol Receiver handshake and shared resource ownership.
 */
#pragma once
#include "negotiation.h"
#include "transport.h"

#include <array>
#include <functional>
#include <memory>
#include <optional>

namespace streamhub {
  /** @brief Issue bounded EINTR/EAGAIN-aware DMA CPU read synchronization. */
  void dma_read_sync(int fd, bool start);
  /** @brief Monotonic timestamp in public protocol nanoseconds. */
  uint64_t monotonic_ns();

  /** @brief Owned descriptor and mapping; never constructs Provider-owned queue objects. */
  class mapped_resource {
  public:
    /** @brief Map a validated resource with the requested protection. */
    mapped_resource(unique_fd fd, size_t bytes, bool writable);
    /** @brief Unmap before closing the descriptor. */
    ~mapped_resource();
    mapped_resource(const mapped_resource &) = delete;
    mapped_resource &operator=(const mapped_resource &) = delete;
    unique_fd descriptor;  ///< Sole local descriptor owner.
    void *data;  ///< Shared mapping.
    size_t bytes;  ///< Exact mapped length.
  };

  /** @brief Validate thirteen independent backings and provide typed queue views. */
  class resources {
  public:
    using sync_fn = std::function<void(int, bool)>;  ///< Injectable syscall boundary for hardware-free tests.
    /** @brief Capture allocation budget and PCM format. */
    resources(protocal::connect_request_info request, sync_fn sync = dma_read_sync);
    /** @brief Validate and adopt one RESOURCE; duplicate slots/backings are errors. */
    void add(const protocal::resource_info &info, unique_fd fd);
    /** @brief Verify completeness, equal video capacities and initialized empty queues. */
    void finish();
    /** @brief Retrieve an owned mapping by internal fixed index (queues 0..3, video 4..11, PCM 12). */
    std::shared_ptr<mapped_resource> at(size_t index) const;
    /** @brief Synchronize a video slot on the consumer execution context. */
    void sync(size_t slot, bool start) const;
    /** @brief Return number of adopted resources. */
    size_t count() const;

  private:
    protocal::connect_request_info request_;  ///< Exact accepted resource requirements.
    sync_fn sync_;  ///< DMA CPU synchronization implementation.
    std::array<std::shared_ptr<mapped_resource>, 13> mappings_ {};  ///< Session-exclusive resources.
    std::array<std::pair<uint64_t, uint64_t>, 13> identities_ {};  ///< Device/inode uniqueness.
    uint64_t total_ = 0;  ///< Accumulated budget.
  };

  /** @brief Synchronous control owner, called only on one session worker at a time. */
  class receiver {
  public:
    /** @brief Complete CONNECT/RESOURCE/READY/CONNECTED within a bounded, cancellable handshake. */
    receiver(const std::string &socket, const protocal::connect_request_info &request, std::stop_token stop = {}, resources::sync_fn sync = dma_read_sync, std::chrono::milliseconds timeout = std::chrono::seconds(5));
    /** @brief Release local state; caller must stop media reads before destruction. */
    ~receiver();
    /** @brief Process ready control packets; false means Provider stopped this session. */
    bool poll();

    /** @brief Borrow the sole control socket for readiness waiting. */
    int native_socket() const {
      return connection_.native_socket();
    }

    /** @brief Next acknowledgement deadline, or no timed work. */
    transport::deadline next_deadline() const {
      return idr_request_ ? idr_deadline_ : transport::deadline::max();
    }

    /** @brief Request an IDR, allowing only one unacknowledged request at a time. */
    void request_idr();
    /** @brief End a session after all local queue and payload accesses have finished. */
    void stop();

    /** @brief Return the negotiated session identity. */
    uint64_t id() const {
      return session_;
    }

    /** @brief Return exact accepted media and optional capabilities. */
    const protocal::connect_accept_info &accepted() const {
      return accepted_;
    }

    /** @brief Return shared resource ownership. */
    std::shared_ptr<resources> memory() const {
      return resources_;
    }

  private:
    /** @brief Send a new request and return its correlation ID. */
    uint64_t send(protocal::control_message_type type, protocal::control_body body, std::stop_token stop = {});
    /** @brief Validate asynchronous STATUS and INPUT_LIST messages. */
    bool event(const protocal::control_message &m);
    transport connection_;  ///< Sole control connection.
    uint64_t request_ = 0, session_ = 0;  ///< Correlation and session identities.
    uint64_t idr_request_ = 0;  ///< Outstanding IDR acknowledgement.
    transport::deadline idr_deadline_ {};  ///< Bounded control response wait.
    protocal::connect_accept_info accepted_ {};  ///< Validated ACCEPT.
    std::shared_ptr<resources> resources_;  ///< Shared mapping lifetime for downstream packets.
    bool connected_ = false;  ///< Completed READY handshake.
  };
}  // namespace streamhub
