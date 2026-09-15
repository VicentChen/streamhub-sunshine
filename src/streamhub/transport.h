/**
 * @file src/streamhub/transport.h
 * @brief Linux control transport for the public MIT StreamHub protocol.
 */
#pragma once
#include <chrono>
#include <stop_token>
#include <streamhub-protocal/protocol.hpp>
#include <string>

namespace streamhub {
  /** @brief Exclusive ownership of a Linux descriptor. */
  class unique_fd {
  public:
    /** @brief Adopt a descriptor, or construct empty. */
    explicit unique_fd(int fd = -1) noexcept;
    /** @brief Close the owned descriptor. */
    ~unique_fd();
    unique_fd(const unique_fd &) = delete;
    unique_fd &operator=(const unique_fd &) = delete;
    /** @brief Transfer descriptor ownership. */
    unique_fd(unique_fd &&other) noexcept;
    /** @brief Replace ownership with another descriptor. */
    unique_fd &operator=(unique_fd &&other) noexcept;
    /** @brief Return the borrowed descriptor. */
    int get() const noexcept;

  private:
    int fd_;  ///< Owned descriptor; -1 means empty.
  };

  /** @brief A decoded packet and its optional RESOURCE descriptor. */
  struct control_packet {
    protocal::control_message message;  ///< Public protocol value.
    unique_fd resource;  ///< Owned RESOURCE fd, empty for other messages.
  };

  /**
   * @brief Nonblocking SEQPACKET connection; serialize operations per connection.
   *
   * All operations use absolute deadlines and throw on cancellation, timeout,
   * EOF or invalid packets. Discard a connection after a receive error.
   * No Provider implementation is linked or included.
   */
  class transport {
  public:
    using deadline = std::chrono::steady_clock::time_point;  ///< Absolute operation deadline.
    /**
     * @brief Connect to a filesystem socket and verify the peer has our effective UID.
     * @param path Provider socket path.
     * @param until Absolute connection deadline.
     * @param stop Cancellation token.
     */
    transport(const std::string &path, deadline until, std::stop_token stop = {});
    /**
     * @brief Encode and send one complete packet without transferring fd ownership.
     * @param message Public protocol message.
     * @param until Absolute send deadline.
     * @param stop Cancellation token.
     * @param resource Borrowed fd, required only for RESOURCE.
     */
    void send(const protocal::control_message &message, deadline until, std::stop_token stop = {}, int resource = -1);
    /**
     * @brief Receive and validate one packet, adopting its RESOURCE fd.
     * @param until Absolute receive deadline.
     * @param stop Cancellation token.
     * @return Decoded packet with exclusive resource ownership.
     */
    /** @brief Report readable control data or peer closure without consuming a packet. */
    bool pending() const;

    /** @brief Borrow the socket for multiplexed readiness; never close it externally. */
    int native_socket() const {
      return socket_.get();
    }

    control_packet receive(deadline until, std::stop_token stop = {});

  private:
    unique_fd socket_;  ///< Connected nonblocking close-on-exec socket.
  };
}  // namespace streamhub
