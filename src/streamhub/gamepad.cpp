/**
 * @file src/streamhub/gamepad.cpp
 * @brief Explicit button/axis conversion with bounded controller backpressure.
 */
#include "gamepad.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace streamhub {
  namespace p = protocal;

  uint32_t gamepad_bridge::buttons(uint32_t moonlight) {
    // Indexed by public physical-position bits; Moonlight constants are protocol values.
    constexpr uint32_t mapping[] {0x1000, 0x2000, 0x4000, 0x8000, 0x1, 0x2, 0x4, 0x8, 0x100, 0x200, 0x40, 0x80, 0x10, 0x20, 0x400, 0x100000, 0x10000, 0x20000, 0x40000, 0x80000, 0x200000};
    uint32_t result = 0;
    for (size_t bit = 0; bit < std::size(mapping); ++bit) {
      if (moonlight & mapping[bit]) {
        result |= 1u << bit;
      }
    }
    return result;
  }

  gamepad_bridge::gamepad_bridge(std::shared_ptr<resources> memory, p::gamepad_service_info service):
      memory_(std::move(memory)),
      service_(service),
      input_(*static_cast<p::gamepad_input_queue *>(memory_->at(2)->data)),
      feedback_(*static_cast<p::gamepad_feedback_queue *>(memory_->at(3)->data)) {}

  void gamepad_bridge::arrival(uint8_t index, uint32_t supported, bool rumble) {
    if (index >= devices_.size()) {
      throw std::runtime_error("StreamHub controller index");
    }
    if (devices_[index].id) {
      return;
    }
    devices_[index].buttons = buttons(supported);
    devices_[index].rumble = rumble;
  }

  void gamepad_bridge::enqueue(p::gamepad_input_info event) {
    if (pending_.size() == 64) {
      throw std::runtime_error("StreamHub controller backlog exhausted");
    }
    event.event_time_ns = monotonic_ns();
    pending_.push_back(event);
  }

  void gamepad_bridge::state(uint8_t index, uint16_t mask, const controller_state &state) {
    if (index >= devices_.size()) {
      throw std::runtime_error("StreamHub controller index");
    }
    if (!service_.max_controllers || !(service_.input_features & p::gamepad_input_features::basic_state)) {
      return;
    }
    for (size_t i = 0; i < devices_.size(); ++i) {
      if (devices_[i].id && !(mask & (1u << i))) {
        p::gamepad_input_info event {};
        event.event_type = p::gamepad_event_type::disconnected;
        event.controller_id = devices_[i].id;
        event.payload.empty = {};
        enqueue(event);
        devices_[i] = {};
      }
    }
    if (!(mask & (1u << index))) {
      return;
    }
    auto &device = devices_[index];
    if (!device.id) {
      auto active = std::count_if(devices_.begin(), devices_.end(), [](const auto &d) {
        return d.id != 0;
      });
      if (active >= service_.max_controllers || next_id_ == UINT32_MAX) {
        throw std::runtime_error("StreamHub controller capacity exceeded");
      }
      device.id = next_id_++;
      p::gamepad_input_info event {};
      event.event_type = p::gamepad_event_type::connected;
      event.controller_id = device.id;
      event.payload.connected = {device.buttons, p::gamepad_input_features::basic_state, device.rumble ? service_.feedback_features & p::gamepad_feedback_features::rumble : 0, {}};
      enqueue(event);
    }
    // Negate in 32 bits and saturate because +32768 cannot fit into the public signed axis.
    auto y = [](int16_t value) {
      return int16_t(std::clamp(-int32_t(value), -32768, 32767));
    };
    p::gamepad_input_info event {};
    event.event_type = p::gamepad_event_type::state;
    event.controller_id = device.id;
    event.payload.state = {buttons(state.buttons) & device.buttons, state.left_x, y(state.left_y), state.right_x, y(state.right_y), uint16_t(state.left_trigger * 257u), uint16_t(state.right_trigger * 257u), {}};
    enqueue(event);
  }

  void gamepad_bridge::flush() {
    while (!pending_.empty() && !input_.full()) {
      input_.enqueue(pending_.front());
      pending_.pop_front();
    }
    if (pending_.empty()) {
      blocked_.reset();
      return;
    }
    auto now = std::chrono::steady_clock::now();
    if (!blocked_) {
      blocked_ = now;
    } else if (now - *blocked_ >= std::chrono::seconds(1)) {
      throw std::runtime_error("StreamHub controller queue stalled");
    }
  }

  std::optional<controller_rumble> gamepad_bridge::feedback() {
    if (feedback_.empty()) {
      return {};
    }
    const auto event = feedback_.front();
    feedback_.dequeue();
    auto device = std::find_if(devices_.begin(), devices_.end(), [&](const auto &d) {
      return d.id && d.id == event.controller_id;
    });
    if (device == devices_.end()) {
      return {};
    }
    if (event.reserved || event.feedback_type != p::gamepad_feedback_type::rumble || !(service_.feedback_features & p::gamepad_feedback_features::rumble) || !device->rumble || std::any_of(event.payload.rumble.reserved.begin(), event.payload.rumble.reserved.end(), [](auto b) {
          return b != 0;
        }) ||
        event.event_time_ns > monotonic_ns() + 1000000000ULL) {
      throw std::runtime_error("StreamHub unsupported controller feedback");
    }
    return controller_rumble {uint8_t(device - devices_.begin()), event.payload.rumble.low_frequency, event.payload.rumble.high_frequency};
  }
}  // namespace streamhub
