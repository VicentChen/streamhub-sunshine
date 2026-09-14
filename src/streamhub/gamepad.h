/**
 * @file src/streamhub/gamepad.h
 * @brief Session-local basic controller and rumble conversion.
 */
#pragma once
#include "receiver.h"

#include <deque>

namespace streamhub {
  /** @brief Moonlight state values at the adapter boundary. */
  struct controller_state {
    uint32_t buttons;  ///< Moonlight button mask.
    uint8_t left_trigger, right_trigger;  ///< Unsigned 8-bit triggers.
    int16_t left_x, left_y, right_x, right_y;  ///< Moonlight stick coordinates.
  };

  /** @brief Rumble feedback addressed to a current Moonlight controller index. */
  struct controller_rumble {
    uint8_t index;
    uint16_t low, high;
  };

  /** @brief One producer for controller events and one consumer for feedback. */
  class gamepad_bridge {
  public:
    /** @brief Bind the two shared queues and actual negotiated intersection. */
    gamepad_bridge(std::shared_ptr<resources> memory, protocal::gamepad_service_info service);
    /** @brief Record optional device capability metadata; connection is emitted with its first state. */
    void arrival(uint8_t index, uint32_t buttons, bool rumble);
    /** @brief Reconcile active controllers, then enqueue a complete state. */
    void state(uint8_t index, uint16_t active_mask, const controller_state &state);
    /** @brief Flush pending events without blocking control processing; persistent fullness throws. */
    void flush();
    /** @brief Read and convert one feedback event; retired IDs are discarded. */
    std::optional<controller_rumble> feedback();

    /** @brief Whether another client packet can be consumed without growing the local bound. */
    bool ready() const {
      return pending_.size() < 32;
    }

    /** @brief Translate physical button positions explicitly. */
    static uint32_t buttons(uint32_t moonlight);

  private:
    /** @brief One client controller's device limits and nonreused protocol identity. */
    struct device {
      uint32_t id = 0, buttons = 0x3fffff;
      bool rumble = true;
    };

    /** @brief Enqueue a fully initialized generic event within the local bound. */
    void enqueue(protocal::gamepad_input_info event);
    std::shared_ptr<resources> memory_;  ///< Queue ownership.
    protocal::gamepad_service_info service_;  ///< Actual session capability intersection.
    protocal::gamepad_input_queue::producer input_;  ///< Sole producer.
    protocal::gamepad_feedback_queue::consumer feedback_;  ///< Sole feedback consumer.
    std::array<device, 16> devices_ {};  ///< Client-relative slots.
    uint32_t next_id_ = 1;  ///< Never reused within this session.
    std::deque<protocal::gamepad_input_info> pending_;  ///< Bounded ordered backlog.
    std::optional<std::chrono::steady_clock::time_point> blocked_;  ///< Full-queue deadline.
  };
}  // namespace streamhub
