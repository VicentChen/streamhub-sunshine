/**
 * @file src/input.h
 * @brief Moonlight controller message decoding, independent of local input devices.
 */
#pragma once
#include "platform/common.h"

#include <atomic>
#include <optional>
#include <span>
#include <variant>

namespace input {
  /**
   * @brief Whether the connected input source accepts controller touch and motion events.
   *
   * The protocol adapter sets this from the source capabilities and clears it on
   * disconnect. It defaults to false while no source is connected. Moonlight gates
   * both controller touch and motion messages on LI_FF_CONTROLLER_TOUCH_EVENTS.
   */
  inline std::atomic_bool supports_controller_touch_events {false};

  /** @brief Controller state, including the client active-controller mask. */
  struct gamepad_state_t {
    std::uint16_t active_mask;
    platf::gamepad_state_t state;
  };

  /** @brief Decoded controller event with a client-relative index. */
  struct gamepad_event_t {
    std::uint8_t controller;
    std::variant<gamepad_state_t, platf::gamepad_arrival_t, platf::gamepad_touch_t, platf::gamepad_motion_t, platf::gamepad_battery_t> data;
  };

  /** @brief Decode one controller packet; malformed or other input types return nullopt. */
  std::optional<gamepad_event_t> decode(std::span<const std::uint8_t> bytes);

  /** @brief Controller messages belonging to one transport session. */
  struct input_t {
    safe::mail_raw_t::queue_t<gamepad_event_t> events;
  };

  /** @brief Allocate session-local protocol state without opening host devices. */
  std::shared_ptr<input_t> alloc(safe::mail_t mail);
  /** @brief Decode and enqueue a controller packet. */
  void passthrough(std::shared_ptr<input_t> &input, std::vector<std::uint8_t> &&bytes);
  /** @brief Stop delivery for the disconnected session. */
  void reset(std::shared_ptr<input_t> &input);
}  // namespace input
