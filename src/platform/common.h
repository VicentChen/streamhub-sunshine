/**
 * @file src/platform/common.h
 * @brief Declarations for common platform specific utilities.
 */
#pragma once

// standard includes
#include <bitset>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>

// lib includes
#include <boost/core/noncopyable.hpp>
#ifndef _WIN32
  #include <boost/asio.hpp>
#endif

// local includes
#include "src/config.h"
#include "src/logging.h"
#include "src/thread_safe.h"
#include "src/utility.h"

extern "C" {
#include <moonlight-common-c/src/Limelight.h>
}

using namespace std::literals;

struct sockaddr;
#ifdef _WIN32
// Forward declarations of boost classes to avoid having to include boost headers
// here, which results in issues with Windows.h and WinSock2.h include order.
namespace boost {
  namespace asio {
    namespace ip {
      class address;
    }  // namespace ip
  }  // namespace asio

  namespace filesystem {
    class path;
  }

}  // namespace boost
#endif
namespace platf {
  // Limited by bits in activeGamepadMask
  constexpr auto MAX_GAMEPADS = 16;  ///< Maximum number of simultaneously tracked gamepads.

  constexpr std::uint32_t DPAD_UP = 0x0001;  ///< Moonlight gamepad button mask bit for D-pad up.
  constexpr std::uint32_t DPAD_DOWN = 0x0002;  ///< Moonlight gamepad button mask bit for D-pad down.
  constexpr std::uint32_t DPAD_LEFT = 0x0004;  ///< Moonlight gamepad button mask bit for D-pad left.
  constexpr std::uint32_t DPAD_RIGHT = 0x0008;  ///< Moonlight gamepad button mask bit for D-pad right.
  constexpr std::uint32_t START = 0x0010;  ///< Moonlight gamepad button mask bit for Start.
  constexpr std::uint32_t BACK = 0x0020;  ///< Moonlight gamepad button mask bit for Back.
  constexpr std::uint32_t LEFT_STICK = 0x0040;  ///< Moonlight gamepad button mask bit for left stick press.
  constexpr std::uint32_t RIGHT_STICK = 0x0080;  ///< Moonlight gamepad button mask bit for right stick press.
  constexpr std::uint32_t LEFT_BUTTON = 0x0100;  ///< Moonlight gamepad button mask bit for left shoulder.
  constexpr std::uint32_t RIGHT_BUTTON = 0x0200;  ///< Moonlight gamepad button mask bit for right shoulder.
  constexpr std::uint32_t HOME = 0x0400;  ///< Moonlight gamepad button mask bit for Home.
  constexpr std::uint32_t A = 0x1000;  ///< Moonlight gamepad button mask bit for A.
  constexpr std::uint32_t B = 0x2000;  ///< Moonlight gamepad button mask bit for B.
  constexpr std::uint32_t X = 0x4000;  ///< Moonlight gamepad button mask bit for X.
  constexpr std::uint32_t Y = 0x8000;  ///< Moonlight gamepad button mask bit for Y.
  constexpr std::uint32_t PADDLE1 = 0x010000;  ///< Moonlight gamepad button mask bit for paddle 1.
  constexpr std::uint32_t PADDLE2 = 0x020000;  ///< Moonlight gamepad button mask bit for paddle 2.
  constexpr std::uint32_t PADDLE3 = 0x040000;  ///< Moonlight gamepad button mask bit for paddle 3.
  constexpr std::uint32_t PADDLE4 = 0x080000;  ///< Moonlight gamepad button mask bit for paddle 4.
  constexpr std::uint32_t TOUCHPAD_BUTTON = 0x100000;  ///< Moonlight gamepad button mask bit for touchpad click.
  constexpr std::uint32_t MISC_BUTTON = 0x200000;  ///< Moonlight gamepad button mask bit for the miscellaneous button.

  /**
   * @brief Enumerates supported gamepad feedback options.
   */
  enum class gamepad_feedback_e {
    rumble,  ///< Rumble
    rumble_triggers,  ///< Rumble triggers
    set_motion_event_state,  ///< Set motion event state
    set_rgb_led,  ///< Set RGB LED
    set_player_leds,  ///< Set player indicator LEDs
    set_adaptive_triggers,  ///< Set adaptive triggers
  };

  /**
   * @brief Feedback command sent from Sunshine to a virtual gamepad.
   */
  struct gamepad_feedback_msg_t {
    /**
     * @brief Create a rumble object or message.
     *
     * @param id Identifier for the controller, session, display, or resource.
     * @param lowfreq Low-frequency rumble motor intensity.
     * @param highfreq High-frequency rumble motor intensity.
     * @return Constructed rumble object.
     */
    static gamepad_feedback_msg_t make_rumble(std::uint16_t id, std::uint16_t lowfreq, std::uint16_t highfreq) {
      gamepad_feedback_msg_t msg;
      msg.type = gamepad_feedback_e::rumble;
      msg.id = id;
      msg.data.rumble = {lowfreq, highfreq};
      return msg;
    }

    /**
     * @brief Create rumble triggers.
     *
     * @param id Identifier for the controller, session, display, or resource.
     * @param left Left trigger or motor payload for the feedback command.
     * @param right Right trigger or motor payload for the feedback command.
     * @return Constructed rumble triggers object.
     */
    static gamepad_feedback_msg_t make_rumble_triggers(std::uint16_t id, std::uint16_t left, std::uint16_t right) {
      gamepad_feedback_msg_t msg;
      msg.type = gamepad_feedback_e::rumble_triggers;
      msg.id = id;
      msg.data.rumble_triggers = {left, right};
      return msg;
    }

    /**
     * @brief Motion-event feedback command payload for a controller.
     *
     * @param id Identifier for the controller, session, display, or resource.
     * @param motion_type Motion type.
     * @param report_rate Report rate.
     * @return Constructed motion event state object.
     */
    static gamepad_feedback_msg_t make_motion_event_state(std::uint16_t id, std::uint8_t motion_type, std::uint16_t report_rate) {
      gamepad_feedback_msg_t msg;
      msg.type = gamepad_feedback_e::set_motion_event_state;
      msg.id = id;
      msg.data.motion_event_state.motion_type = motion_type;
      msg.data.motion_event_state.report_rate = report_rate;
      return msg;
    }

    /**
     * @brief Create RGB led.
     *
     * @param id Identifier for the controller, session, display, or resource.
     * @param r Red color channel value.
     * @param g Green color channel value.
     * @param b Blue color channel value.
     * @return Constructed RGB led object.
     */
    static gamepad_feedback_msg_t make_rgb_led(std::uint16_t id, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
      gamepad_feedback_msg_t msg;
      msg.type = gamepad_feedback_e::set_rgb_led;
      msg.id = id;
      msg.data.rgb_led = {r, g, b};
      return msg;
    }

    /**
     * @brief Create player indicator LED state.
     *
     * @param id Identifier for the controller, session, display, or resource.
     * @param solid Four-bit mask of solid player indicators.
     * @param flashing Four-bit mask of flashing player indicators.
     * @return Constructed player indicator LED object.
     */
    static gamepad_feedback_msg_t make_player_leds(std::uint16_t id, std::uint8_t solid, std::uint8_t flashing) {
      gamepad_feedback_msg_t msg;
      msg.type = gamepad_feedback_e::set_player_leds;
      msg.id = id;
      msg.data.player_leds = {solid, flashing};
      return msg;
    }

    /**
     * @brief Create adaptive triggers.
     *
     * @param id Identifier for the controller, session, display, or resource.
     * @param event_flags Event flags.
     * @param type_left Type left.
     * @param type_right Type right.
     * @param left Left trigger or motor payload for the feedback command.
     * @param right Right trigger or motor payload for the feedback command.
     * @return Constructed adaptive triggers object.
     */
    static gamepad_feedback_msg_t make_adaptive_triggers(std::uint16_t id, uint8_t event_flags, uint8_t type_left, uint8_t type_right, const std::array<uint8_t, 10> &left, const std::array<uint8_t, 10> &right) {
      gamepad_feedback_msg_t msg;
      msg.type = gamepad_feedback_e::set_adaptive_triggers;
      msg.id = id;
      msg.data.adaptive_triggers = {.event_flags = event_flags, .type_left = type_left, .type_right = type_right, .left = left, .right = right};
      return msg;
    }

    gamepad_feedback_e type;  ///< Feedback command type stored in the union payload.
    std::uint16_t id;  ///< Controller identifier associated with this message.

    union {
      struct {
        std::uint16_t lowfreq;  ///< Low-frequency rumble motor intensity.
        std::uint16_t highfreq;  ///< High-frequency rumble motor intensity.
      } rumble;  ///< Main rumble-motor payload.

      struct {
        std::uint16_t left_trigger;  ///< Left-trigger rumble motor intensity.
        std::uint16_t right_trigger;  ///< Right-trigger rumble motor intensity.
      } rumble_triggers;  ///< Trigger-rumble payload.

      struct {
        std::uint16_t report_rate;  ///< Requested motion-sensor report rate.
        std::uint8_t motion_type;  ///< Motion-sensor type to configure.
      } motion_event_state;  ///< Motion-event configuration payload.

      struct {
        std::uint8_t r;  ///< Red LED channel intensity.
        std::uint8_t g;  ///< Green LED channel intensity.
        std::uint8_t b;  ///< Blue LED channel intensity.
      } rgb_led;  ///< RGB LED payload.

      struct {
        std::uint8_t solid;  ///< Bit mask of solid player indicators.
        std::uint8_t flashing;  ///< Bit mask of flashing player indicators.
      } player_leds;  ///< Player-indicator LED payload.

      struct {
        uint16_t controllerNumber;  ///< Controller number supplied to the adaptive-trigger backend.
        uint8_t event_flags;  ///< Flags describing which adaptive-trigger data is present.
        uint8_t type_left;  ///< Left adaptive-trigger effect type.
        uint8_t type_right;  ///< Right adaptive-trigger effect type.
        std::array<uint8_t, 10> left;  ///< Left adaptive-trigger effect parameters.
        std::array<uint8_t, 10> right;  ///< Right adaptive-trigger effect parameters.
      } adaptive_triggers;  ///< Adaptive-trigger effect payload.
    } data;  ///< Controller feedback payload for the selected feedback type.
  };

  /**
   * @brief Queue used to deliver controller feedback commands to the platform backend.
   */
  using feedback_queue_t = safe::mail_raw_t::queue_t<gamepad_feedback_msg_t>;

  namespace speaker {
    /**
     * @brief Enumerates supported speaker options.
     */
    enum speaker_e {
      FRONT_LEFT,  ///< Front left
      FRONT_RIGHT,  ///< Front right
      FRONT_CENTER,  ///< Front center
      LOW_FREQUENCY,  ///< Low frequency
      BACK_LEFT,  ///< Back left
      BACK_RIGHT,  ///< Back right
      SIDE_LEFT,  ///< Side left
      SIDE_RIGHT,  ///< Side right
      MAX_SPEAKERS,  ///< Maximum number of speakers
    };

    /**
     * @brief Moonlight speaker order for stereo audio.
     */
    constexpr std::array<std::uint8_t, 2> map_stereo {
      FRONT_LEFT,
      FRONT_RIGHT
    };
    /**
     * @brief Moonlight speaker order for 5.1 surround audio.
     */
    constexpr std::array<std::uint8_t, 6> map_surround51 {
      FRONT_LEFT,
      FRONT_RIGHT,
      FRONT_CENTER,
      LOW_FREQUENCY,
      BACK_LEFT,
      BACK_RIGHT,
    };
    /**
     * @brief Moonlight speaker order for 7.1 surround audio.
     */
    constexpr std::array<std::uint8_t, 8> map_surround71 {
      FRONT_LEFT,
      FRONT_RIGHT,
      FRONT_CENTER,
      LOW_FREQUENCY,
      BACK_LEFT,
      BACK_RIGHT,
      SIDE_LEFT,
      SIDE_RIGHT,
    };
  }  // namespace speaker

  /**
   * @brief Enumerates supported mem type options.
   */
  struct gamepad_state_t {
    std::uint32_t buttonFlags;  ///< Moonlight button mask for the current gamepad state.
    std::uint8_t lt;  ///< Left trigger value.
    std::uint8_t rt;  ///< Right trigger value.
    std::int16_t lsX;  ///< Left stick X-axis value.
    std::int16_t lsY;  ///< Left stick Y-axis value.
    std::int16_t rsX;  ///< Right stick X-axis value.
    std::int16_t rsY;  ///< Right stick Y-axis value.
  };

  /**
   * @brief Client-relative controller identifier.
   */
  struct gamepad_id_t {
    // The client-relative index is the controller number as reported by the
    // client. It must be used when communicating back to the client via
    // the input feedback queue.
    std::uint8_t clientRelativeIndex;  ///< Client relative index.
  };

  /**
   * @brief Capabilities reported when a controller is connected.
   */
  struct gamepad_arrival_t {
    std::uint8_t type;  ///< Protocol or controller type discriminator.
    std::uint16_t capabilities;  ///< Capability flags advertised by the controller.
    std::uint32_t supportedButtons;  ///< Button mask supported by the connected controller.
  };

  /**
   * @brief Touchpad contact data reported by a controller.
   */
  struct gamepad_touch_t {
    gamepad_id_t id;  ///< Gamepad identifier for the event.
    std::uint8_t eventType;  ///< Moonlight event type for the input packet.
    std::uint32_t pointerId;  ///< Client-provided pointer identifier for a touch contact.
    float x;  ///< Horizontal coordinate or vector component.
    float y;  ///< Vertical coordinate or vector component.
    float pressure;  ///< Contact pressure reported by the client.
  };

  /**
   * @brief Accelerometer or gyroscope sample from a controller.
   */
  struct gamepad_motion_t {
    gamepad_id_t id;  ///< Gamepad identifier for the event.
    std::uint8_t motionType;  ///< Motion type.

    // Accel: m/s^2
    // Gyro: deg/s
    float x;  ///< Horizontal coordinate or vector component.
    float y;  ///< Vertical coordinate or vector component.
    float z;  ///< Depth or Z-axis vector component.
  };

  /**
   * @brief Battery state reported by a virtual gamepad.
   */
  struct gamepad_battery_t {
    gamepad_id_t id;  ///< Gamepad identifier for the event.
    std::uint8_t state;  ///< Battery state reported by the client.
    std::uint8_t percentage;  ///< Battery charge percentage.
  };

  class deinit_t {
  public:
    /**
     * @brief Destroy the deinitializer.
     */
    virtual ~deinit_t() = default;
  };

  /**
   * @brief Return the application configuration directory.
   */
  std::filesystem::path appdata();

  /**
   * @brief Return the hardware MAC address associated with a network address.
   *
   * @param address Network address being parsed or filtered.
   * @return Hardware MAC address string, or an empty string when it cannot be resolved.
   */
  std::string get_mac_address(const std::string_view &address);

  /**
   * @brief Convert a socket address to a printable IP address.
   *
   * @param ip_addr Socket address to format.
   * @return Value converted from sockaddr.
   */
  std::string from_sockaddr(const sockaddr *const);
  /**
   * @brief Convert a socket address to a port and printable IP address.
   *
   * @param ip_addr Socket address to format.
   * @return Value converted from sockaddr ex.
   */
  std::pair<std::uint16_t, std::string> from_sockaddr_ex(const sockaddr *const);

  /**
   * @brief Create the platform audio controller.
   *
   * @return Platform audio controller, or nullptr when audio control is unavailable.
   */
  enum class thread_priority_e : int {
    low,  ///< Low priority
    normal,  ///< Normal priority
    high,  ///< High priority
    critical  ///< Critical priority
  };
  /**
   * @brief Apply the requested scheduling priority to the current thread.
   *
   * @param priority Thread priority requested from the platform backend.
   */
  void adjust_thread_priority(thread_priority_e priority);

  /**
   * @brief Name the current thread for use with development tools.
   * @note On Linux this will be truncated after 15 characters.
   *
   * @param name Human-readable name to assign.
   */
  void set_thread_name(std::string_view name);

  // Allow OS-specific actions to be taken to prepare for streaming

  void restart();

  /**
   * @brief Platform buffer pointer and size for batched socket sends.
   */
  struct buffer_descriptor_t {
    const char *buffer;  ///< Pointer to the payload buffer.
    size_t size;  ///< Size of the buffer in bytes.

    // Constructors required for emplace_back() prior to C++20
    /**
     * @brief Describe a contiguous byte buffer for batched socket sending.
     *
     * @param buffer Serialized byte buffer to read from or write to.
     * @param size Number of bytes or elements requested.
     */
    buffer_descriptor_t(const char *buffer, size_t size):
        buffer(buffer),
        size(size) {
    }

    buffer_descriptor_t():
        buffer(nullptr),
        size(0) {
    }
  };

  /**
   * @brief Buffers and native metadata for one batched send operation.
   */
  struct batched_send_info_t {
    // Optional headers to be prepended to each packet
    const char *headers;  ///< Optional header bytes prepended to each payload.
    size_t header_size;  ///< Header size in bytes.

    // One or more data buffers to use for the payloads
    //
    // NB: Data buffers must be aligned to payload size!
    std::vector<buffer_descriptor_t> &payload_buffers;  ///< Payload buffers containing one or more packet blocks.
    size_t payload_size;  ///< Payload size in bytes for each packet block.

    // The offset (in header+payload message blocks) in the header and payload
    // buffers to begin sending messages from
    size_t block_offset;  ///< First packet block index to send.

    // The number of header+payload message blocks to send
    size_t block_count;  ///< Number of packet blocks to send.

    std::uintptr_t native_socket;  ///< Platform socket handle used for the send operation.
    boost::asio::ip::address &target_address;  ///< Destination IP address for outgoing packets.
    uint16_t target_port;  ///< Destination UDP port for outgoing packets.
    boost::asio::ip::address &source_address;  ///< Local source IP address for outgoing packets.

    /**
     * @brief Returns a payload buffer descriptor for the given payload offset.
     * @param offset The offset in the total payload data (bytes).
     * @return Buffer descriptor describing the region at the given offset.
     */
    buffer_descriptor_t buffer_for_payload_offset(ptrdiff_t offset) {
      for (const auto &desc : payload_buffers) {
        if (offset < desc.size) {
          return {
            desc.buffer + offset,
            desc.size - offset,
          };
        } else {
          offset -= desc.size;
        }
      }
      return {};
    }
  };

  /**
   * @brief Send multiple fixed-size UDP payload blocks using the platform backend.
   *
   * @param send_info Socket addresses, buffers, and sizes for the send operation.
   * @return True when all requested packet blocks are submitted to the socket.
   */
  bool send_batch(batched_send_info_t &send_info);

  /**
   * @brief Destination address and payload data for one UDP send.
   */
  struct send_info_t {
    const char *header;  ///< Optional header bytes prepended to the payload.
    size_t header_size;  ///< Header size in bytes.
    const char *payload;  ///< Payload bytes to send after the header.
    size_t payload_size;  ///< Payload size in bytes for each packet block.

    std::uintptr_t native_socket;  ///< Platform socket handle used for the send operation.
    boost::asio::ip::address &target_address;  ///< Destination IP address for outgoing packets.
    uint16_t target_port;  ///< Destination UDP port for outgoing packets.
    boost::asio::ip::address &source_address;  ///< Local source IP address for outgoing packets.
  };

  /**
   * @brief Send the serialized response over the active socket.
   *
   * @param send_info Socket addresses, buffers, and sizes for the send operation.
   * @return True when the packet is submitted to the socket.
   */
  bool send(send_info_t &send_info);

  /**
   * @brief Identifies traffic classes used for socket QoS tagging.
   */
  enum class qos_data_type_e : int {
    audio,  ///< Audio
    video  ///< Video
  };

  /**
   * @brief Enable QoS on the given socket for traffic to the specified destination.
   * @param native_socket The native socket handle.
   * @param address The destination address for traffic sent on this socket.
   * @param port The destination port for traffic sent on this socket.
   * @param data_type The type of traffic sent on this socket.
   * @param dscp_tagging Specifies whether to enable DSCP tagging on outgoing traffic.
   *
   * @return Cleanup handle that restores or releases QoS state when destroyed.
   */
  std::unique_ptr<deinit_t> enable_socket_qos(uintptr_t native_socket, boost::asio::ip::address &address, uint16_t port, qos_data_type_e data_type, bool dscp_tagging);

  /**
   * @brief Create the platform input backend for a stream.
   *
   * @return Platform-specific input backend for the active stream.
   */
  constexpr auto SERVICE_NAME = "Sunshine";  ///< mDNS service instance name advertised for GameStream discovery.
  constexpr auto SERVICE_TYPE = "_nvstream._tcp";  ///< mDNS service type advertised for GameStream discovery.

  namespace publish {
    [[nodiscard]] std::unique_ptr<deinit_t> start();
  }

  /**
   * @brief Initialize the platform-specific high precision timer.
   *
   * @return Cleanup handle for initialized platform resources, or null if none are needed.
   */
  [[nodiscard]] std::unique_ptr<deinit_t> init();

  /**
   * @brief Returns the current computer name in UTF-8.
   * @return Computer name or a placeholder upon failure.
   */
  std::string get_host_name();

  struct high_precision_timer: private boost::noncopyable {
    virtual ~high_precision_timer() = default;

    /**
     * @brief Sleep for the duration
     * @param duration Sleep duration
     */
    virtual void sleep_for(const std::chrono::nanoseconds &duration) = 0;

    /**
     * @brief Check if platform-specific timer backend has been initialized successfully
     * @return `true` on success, `false` on error
     */
    virtual operator bool() = 0;
  };

  /**
   * @brief Create platform-specific timer capable of high-precision sleep
   * @return A unique pointer to timer
   */
  std::unique_ptr<high_precision_timer> create_high_precision_timer();

}  // namespace platf
