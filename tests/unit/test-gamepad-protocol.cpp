/** @file tests/unit/test-gamepad-protocol.cpp
 * @brief Controller wire decoding without local device access.
 */
#include "../tests_common.h"
#include "src/input.h"
#include "src/rtsp.h"

#include <boost/asio.hpp>
#include <moonlight-common-c/src/Limelight.h>
extern "C" {
#include <moonlight-common-c/src/Rtsp.h>
}

#include <cstring>
#include <moonlight-common-c/src/Input.h>

namespace {
  template<class Packet>
  std::vector<std::uint8_t> bytes(Packet packet, std::uint32_t magic) {
    packet.header.size = util::endian::big(static_cast<std::uint32_t>(sizeof(packet) - sizeof(packet.header.size)));
    packet.header.magic = util::endian::little(magic);
    std::vector<std::uint8_t> result(sizeof(packet));
    std::memcpy(result.data(), &packet, sizeof(packet));
    return result;
  }
}  // namespace

TEST(GamepadProtocolTest, DecodesButtonsAxesAndActiveMask) {
  NV_MULTI_CONTROLLER_PACKET packet {};
  packet.controllerNumber = util::endian::little<std::int16_t>(3);
  packet.activeGamepadMask = util::endian::little<std::int16_t>(8);
  packet.buttonFlags = util::endian::little<std::uint16_t>(0x8001);
  packet.buttonFlags2 = util::endian::little<std::uint16_t>(0x12);
  packet.leftTrigger = 251;
  packet.leftStickX = util::endian::little<std::int16_t>(-32768);
  const auto event = input::decode(bytes(packet, MULTI_CONTROLLER_MAGIC_GEN5));
  ASSERT_TRUE(event);
  EXPECT_EQ(event->controller, 3);
  const auto &state = std::get<input::gamepad_state_t>(event->data);
  EXPECT_EQ(state.active_mask, 8);
  EXPECT_EQ(state.state.buttonFlags, 0x128001u);
  EXPECT_EQ(state.state.lt, 251);
  EXPECT_EQ(state.state.lsX, -32768);
}

TEST(GamepadProtocolTest, RejectsTruncatedInvalidIndexAndWrongDeclaredSize) {
  NV_MULTI_CONTROLLER_PACKET packet {};
  auto data = bytes(packet, MULTI_CONTROLLER_MAGIC_GEN5);
  for (std::size_t size = 0; size < data.size(); ++size) {
    EXPECT_FALSE(input::decode(std::span(data).first(size)));
  }
  data[3] ^= 1;
  EXPECT_FALSE(input::decode(data));
  packet.controllerNumber = util::endian::little<std::int16_t>(16);
  EXPECT_FALSE(input::decode(bytes(packet, MULTI_CONTROLLER_MAGIC_GEN5)));
}

TEST(GamepadProtocolTest, DecodesArrivalTouchMotionAndBattery) {
  SS_CONTROLLER_ARRIVAL_PACKET arrival {};
  arrival.controllerNumber = 2;
  arrival.capabilities = util::endian::little<std::uint16_t>(0x1234);
  auto event = input::decode(bytes(arrival, SS_CONTROLLER_ARRIVAL_MAGIC));
  ASSERT_TRUE(event);
  EXPECT_EQ(std::get<platf::gamepad_arrival_t>(event->data).capabilities, 0x1234);
  SS_CONTROLLER_TOUCH_PACKET touch {};
  touch.controllerNumber = 2;
  touch.pointerId = util::endian::little<std::uint32_t>(0x12345678);
  std::uint8_t half[] {0, 0, 0, 0x3f};
  std::memcpy(touch.x, half, 4);
  event = input::decode(bytes(touch, SS_CONTROLLER_TOUCH_MAGIC));
  ASSERT_TRUE(event);
  EXPECT_FLOAT_EQ(std::get<platf::gamepad_touch_t>(event->data).x, 0.5f);
  EXPECT_EQ(std::get<platf::gamepad_touch_t>(event->data).pointerId, 0x12345678u);
  SS_CONTROLLER_MOTION_PACKET motion {};
  motion.controllerNumber = 2;
  std::memcpy(motion.z, half, 4);
  event = input::decode(bytes(motion, SS_CONTROLLER_MOTION_MAGIC));
  ASSERT_TRUE(event);
  EXPECT_FLOAT_EQ(std::get<platf::gamepad_motion_t>(event->data).z, 0.5f);
  SS_CONTROLLER_BATTERY_PACKET battery {};
  battery.controllerNumber = 2;
  battery.batteryPercentage = 83;
  event = input::decode(bytes(battery, SS_CONTROLLER_BATTERY_MAGIC));
  ASSERT_TRUE(event);
  EXPECT_EQ(std::get<platf::gamepad_battery_t>(event->data).percentage, 83);
}

TEST(GamepadProtocolTest, SessionQueuesAreIndependentAndStopOnDisconnect) {
  auto first = input::alloc(std::make_shared<safe::mail_raw_t>());
  auto second = input::alloc(std::make_shared<safe::mail_raw_t>());
  SS_CONTROLLER_BATTERY_PACKET packet {};
  input::passthrough(first, bytes(packet, SS_CONTROLLER_BATTERY_MAGIC));
  EXPECT_TRUE(first->events->peek());
  EXPECT_FALSE(second->events->peek());
  auto queue = first->events;
  input::reset(first);
  EXPECT_FALSE(first);
  EXPECT_FALSE(queue->running());
  EXPECT_TRUE(second->events->running());
}

namespace rtsp_stream {
  class rtsp_server_t;
  void free_msg(PRTSP_MESSAGE msg);
  using msg_t = util::safe_ptr<RTSP_MESSAGE, free_msg>;
  void cmd_describe(rtsp_server_t *server, boost::asio::ip::tcp::socket &sock, launch_session_t &session, msg_t &&req);
}  // namespace rtsp_stream

TEST(GamepadProtocolTest, DescribeTracksSourceTouchAndMotionSupport) {
  const auto previous_support = input::supports_controller_touch_events.load();
  auto restore = util::fail_guard([&] {
    input::supports_controller_touch_events.store(previous_support);
  });
  EXPECT_FALSE(previous_support);

  // Exercise the real DESCRIBE handler and wire response, including disconnect.
  for (bool supported : {false, true, false}) {
    input::supports_controller_touch_events.store(supported);
    boost::asio::io_context io;
    using boost::asio::ip::tcp;
    tcp::acceptor acceptor {io, {boost::asio::ip::address_v4::loopback(), 0}};
    tcp::socket client {io};
    client.connect(acceptor.local_endpoint());
    auto server = acceptor.accept();

    rtsp_stream::launch_session_t session {};
    rtsp_stream::msg_t request {new RTSP_MESSAGE {}};
    request->sequenceNumber = 7;
    rtsp_stream::cmd_describe(nullptr, server, session, std::move(request));
    server.close();

    std::string response;
    boost::system::error_code error;
    boost::asio::read(client, boost::asio::dynamic_buffer(response), error);
    ASSERT_EQ(error, boost::asio::error::eof);
    ASSERT_EQ(response.find("RTSP/1.0 200 OK"), 0);
    ASSERT_NE(response.find("CSeq: 7"), std::string::npos);

    const std::string attribute = "a=x-ss-general.featureFlags:";
    const auto position = response.find(attribute);
    ASSERT_NE(position, std::string::npos);
    const auto flags = std::stoul(response.substr(position + attribute.size()));
    // Moonlight checks this same bit for both touch and motion send functions.
    EXPECT_EQ((flags & LI_FF_CONTROLLER_TOUCH_EVENTS) != 0, supported);
    EXPECT_EQ(flags & ~LI_FF_CONTROLLER_TOUCH_EVENTS, 0u);
  }
}
