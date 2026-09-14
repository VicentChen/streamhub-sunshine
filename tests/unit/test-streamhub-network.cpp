/**
 * @file tests/unit/test-streamhub-network.cpp
 * @brief Verify the real Linux send wait against a saturated kernel socket queue.
 */
#ifdef __linux__
  #include "../tests_common.h"
  #include "src/streamhub/transport.h"

  #include <array>
  #include <cerrno>
  #include <sys/socket.h>

namespace platf {
  bool wait_socket_writable(int fd, std::chrono::steady_clock::time_point deadline);
}

TEST(StreamHubNetworkTest, SaturatedSocketReturnsAtDeadlineAndRecovers) {
  int descriptors[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0, descriptors), 0);
  streamhub::unique_fd writer(descriptors[0]), reader(descriptors[1]);
  int size = 4096;
  ASSERT_EQ(setsockopt(writer.get(), SOL_SOCKET, SO_SNDBUF, &size, sizeof(size)), 0);
  std::array<char, 1024> data {};
  int sent = 0;
  while (send(writer.get(), data.data(), data.size(), MSG_DONTWAIT | MSG_NOSIGNAL) > 0) {
    ASSERT_LT(++sent, 1000);
  }
  ASSERT_EQ(errno, EAGAIN);
  auto begin = std::chrono::steady_clock::now();
  EXPECT_FALSE(platf::wait_socket_writable(writer.get(), begin + std::chrono::milliseconds(50)));
  EXPECT_LT(std::chrono::steady_clock::now() - begin, std::chrono::milliseconds(500));
  while (recv(reader.get(), data.data(), data.size(), MSG_DONTWAIT) > 0) {}
  EXPECT_TRUE(platf::wait_socket_writable(writer.get(), std::chrono::steady_clock::now() + std::chrono::milliseconds(50)));
}
#endif
