/**
 * @file src/streamhub/transport.cpp
 * @brief Cancellable Linux SEQPACKET and SCM_RIGHTS transport.
 */
#include "transport.h"

#include <array>
#include <cerrno>
#include <cstring>
#include <poll.h>
#include <stdexcept>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <vector>

namespace streamhub {
  namespace {
    /** @brief Throw a diagnostic retaining the Linux error code. */
    void system_failure(const char *operation) {
      throw std::system_error(errno, std::generic_category(), operation);
    }

    /** @brief Wake a poll immediately when the associated token is cancelled. */
    class waiter {
    public:
      /** @brief Create a private cancellation event and register its wake callback. */
      explicit waiter(std::stop_token stop):
          event_(eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)),
          callback_(stop, wake {event_.get()}),
          stop_(stop) {
        if (event_.get() < 0) {
          system_failure("StreamHub eventfd");
        }
      }

      /** @brief Check cancellation/deadline even when socket operations succeed immediately. */
      void check(transport::deadline until) const {
        if (stop_.stop_requested()) {
          throw std::runtime_error("StreamHub operation cancelled");
        }
        if (std::chrono::steady_clock::now() >= until) {
          throw std::runtime_error("StreamHub operation timed out");
        }
      }

      /** @brief Wait for socket readiness, cancellation, or the absolute deadline. */
      void wait(int fd, short events, transport::deadline until) const {
        for (;;) {
          check(until);
          auto left = until - std::chrono::steady_clock::now();
          auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(left).count();
          if (ns <= 0) {
            continue;
          }
          timespec timeout {ns / 1000000000, ns % 1000000000};
          pollfd fds[] {{fd, events, 0}, {event_.get(), POLLIN, 0}};
          int result = ppoll(fds, 2, &timeout, nullptr);
          if (result < 0 && errno != EINTR) {
            system_failure("StreamHub ppoll");
          }
          check(until);
          if (result > 0 && fds[0].revents) {
            if (fds[0].revents & POLLNVAL) {
              throw std::runtime_error("StreamHub invalid socket");
            }
            return;
          }
        }
      }

    private:
      /** @brief Stop callback that never throws or blocks. */
      struct wake {
        int fd;  ///< Borrowed eventfd.

        /** @brief Signal cancellation; EAGAIN already means signalled. */
        void operator()() const noexcept {
          uint64_t value = 1;
          while (write(fd, &value, sizeof(value)) < 0 && errno == EINTR) {}
        }
      };

      unique_fd event_;  ///< Declared before callback so callback unregisters before close.
      std::stop_callback<wake> callback_;  ///< Wakes blocked ppoll.
      std::stop_token stop_;  ///< Cancellation state.
    };
  }  // namespace

  unique_fd::unique_fd(int fd) noexcept:
      fd_(fd) {}

  unique_fd::~unique_fd() {
    if (fd_ >= 0) {
      close(fd_);
    }
  }

  unique_fd::unique_fd(unique_fd &&other) noexcept:
      fd_(std::exchange(other.fd_, -1)) {}

  unique_fd &unique_fd::operator=(unique_fd &&other) noexcept {
    if (this != &other) {
      unique_fd old(std::exchange(fd_, std::exchange(other.fd_, -1)));
    }
    return *this;
  }

  int unique_fd::get() const noexcept {
    return fd_;
  }

  transport::transport(const std::string &path, deadline until, std::stop_token stop):
      socket_(socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC, 0)) {
    if (socket_.get() < 0) {
      system_failure("StreamHub socket");
    }
    sockaddr_un address {};
    if (path.empty() || path.front() != '/' || path.size() >= sizeof(address.sun_path) || path.find('\0') != std::string::npos) {
      throw std::invalid_argument("StreamHub requires an absolute filesystem socket path");
    }
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    waiter pending(stop);
    pending.check(until);
    if (connect(socket_.get(), reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0) {
      // AF_UNIX EAGAIN means backlog full, not an in-progress connection.
      if (errno != EINPROGRESS) {
        system_failure("StreamHub connect");
      }
      pending.wait(socket_.get(), POLLOUT, until);
      int error = 0;
      socklen_t size = sizeof(error);
      if (getsockopt(socket_.get(), SOL_SOCKET, SO_ERROR, &error, &size) < 0) {
        system_failure("StreamHub SO_ERROR");
      }
      if (error) {
        throw std::system_error(error, std::generic_category(), "StreamHub connect");
      }
    }
    ucred peer {};
    socklen_t size = sizeof(peer);
    if (getsockopt(socket_.get(), SOL_SOCKET, SO_PEERCRED, &peer, &size) < 0) {
      system_failure("StreamHub SO_PEERCRED");
    }
    if (size != sizeof(peer) || peer.uid != geteuid()) {
      throw std::runtime_error("StreamHub Provider UID mismatch");
    }
  }

  void transport::send(const protocal::control_message &message, deadline until, std::stop_token stop, int resource) {
    std::vector<std::byte> bytes;
    if (protocal::encode_control(message, bytes) != protocal::control_error::none || (message.header.message_type == protocal::control_message_type::resource) != (resource >= 0)) {
      throw std::invalid_argument("StreamHub invalid outgoing control packet");
    }
    iovec io {bytes.data(), bytes.size()};
    alignas(cmsghdr) std::array<std::byte, CMSG_SPACE(sizeof(int))> ancillary {};
    msghdr packet {};
    packet.msg_iov = &io;
    packet.msg_iovlen = 1;
    if (resource >= 0) {
      packet.msg_control = ancillary.data();
      packet.msg_controllen = ancillary.size();
      auto *header = CMSG_FIRSTHDR(&packet);
      header->cmsg_level = SOL_SOCKET;
      header->cmsg_type = SCM_RIGHTS;
      header->cmsg_len = CMSG_LEN(sizeof(int));
      std::memcpy(CMSG_DATA(header), &resource, sizeof(resource));
    }
    waiter pending(stop);
    for (;;) {
      pending.check(until);
      auto count = sendmsg(socket_.get(), &packet, MSG_NOSIGNAL);
      if (count >= 0) {
        if (static_cast<size_t>(count) != bytes.size()) {
          throw std::runtime_error("StreamHub partial SEQPACKET send");
        }
        return;
      }
      if (errno == EINTR) {
        continue;
      }
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
        system_failure("StreamHub sendmsg");
      }
      pending.wait(socket_.get(), POLLOUT, until);
    }
  }

  bool transport::pending() const {
    pollfd event {socket_.get(), POLLIN, 0};
    int result;
    do {
      result = poll(&event, 1, 0);
    } while (result < 0 && errno == EINTR);
    if (result < 0) {
      system_failure("StreamHub poll");
    }
    return result > 0;
  }

  control_packet transport::receive(deadline until, std::stop_token stop) {
    std::array<std::byte, protocal::max_control_bytes> bytes;
    // Linux accepts at most 253 SCM_RIGHTS fds per packet. Adopt all before validation.
    alignas(cmsghdr) std::array<std::byte, CMSG_SPACE(253 * sizeof(int))> ancillary {};
    std::array<unique_fd, 253> descriptors;
    waiter pending(stop);
    for (;;) {
      pending.check(until);
      iovec io {bytes.data(), bytes.size()};
      msghdr packet {};
      packet.msg_iov = &io;
      packet.msg_iovlen = 1;
      packet.msg_control = ancillary.data();
      packet.msg_controllen = ancillary.size();
      auto count = recvmsg(socket_.get(), &packet, MSG_CMSG_CLOEXEC);
      if (count < 0) {
        if (errno == EINTR) {
          continue;
        }
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
          system_failure("StreamHub recvmsg");
        }
        pending.wait(socket_.get(), POLLIN, until);
        continue;
      }
      size_t fd_count = 0;
      bool invalid_ancillary = false;
      for (auto *header = CMSG_FIRSTHDR(&packet); header; header = CMSG_NXTHDR(&packet, header)) {
        if (header->cmsg_level != SOL_SOCKET || header->cmsg_type != SCM_RIGHTS || header->cmsg_len < CMSG_LEN(0)) {
          invalid_ancillary = true;
          continue;
        }
        auto size = header->cmsg_len - CMSG_LEN(0);
        invalid_ancillary |= size % sizeof(int) != 0;
        for (size_t offset = 0; offset + sizeof(int) <= size; offset += sizeof(int)) {
          int fd;
          std::memcpy(&fd, CMSG_DATA(header) + offset, sizeof(fd));
          unique_fd owned(fd);
          if (fd_count < descriptors.size()) {
            descriptors[fd_count++] = std::move(owned);
          } else {
            invalid_ancillary = true;
          }
        }
      }
      if (count == 0) {
        throw std::runtime_error("StreamHub control EOF");
      }
      if (invalid_ancillary || (packet.msg_flags & (MSG_TRUNC | MSG_CTRUNC))) {
        throw std::runtime_error("StreamHub truncated or invalid ancillary packet");
      }
      control_packet result;
      if (protocal::decode_control(std::span(bytes.data(), static_cast<size_t>(count)), result.message) != protocal::control_error::none) {
        throw std::runtime_error("StreamHub invalid control packet");
      }
      size_t expected = result.message.header.message_type == protocal::control_message_type::resource ? 1 : 0;
      if (fd_count != expected) {
        throw std::runtime_error("StreamHub unexpected resource fd count");
      }
      if (expected) {
        result.resource = std::move(descriptors[0]);
      }
      return result;
    }
  }
}  // namespace streamhub
