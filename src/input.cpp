/**
 * @file src/input.cpp
 * @brief Moonlight controller protocol decoding.
 */
#include "input.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <moonlight-common-c/src/Input.h>

namespace input {
  namespace {
    template<class Packet, class Convert>
    std::optional<gamepad_event_t> read(std::span<const std::uint8_t> bytes, Convert convert) {
      if (bytes.size() < sizeof(Packet)) {
        return std::nullopt;
      }
      Packet packet;
      std::memcpy(&packet, bytes.data(), sizeof(packet));
      if (util::endian::big(packet.header.size) != sizeof(Packet) - sizeof(packet.header.size)) {
        return std::nullopt;
      }
      auto index = util::endian::little(packet.controllerNumber);
      if (index < 0 || index >= platf::MAX_GAMEPADS) {
        return std::nullopt;
      }
      return gamepad_event_t {static_cast<std::uint8_t>(index), convert(packet)};
    }

    float net_float(netfloat value) {
      std::uint32_t bits;
      std::memcpy(&bits, value, sizeof(bits));
      return std::bit_cast<float>(util::endian::little(bits));
    }

    float unit_float(netfloat value) {
      auto f = net_float(value);
      return std::isfinite(f) ? std::clamp(f, 0.0f, 1.0f) : 0.0f;
    }
  }  // namespace

  std::optional<gamepad_event_t> decode(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < sizeof(NV_INPUT_HEADER)) {
      return std::nullopt;
    }
    NV_INPUT_HEADER header;
    std::memcpy(&header, bytes.data(), sizeof(header));
    switch (util::endian::little(header.magic)) {
      case MULTI_CONTROLLER_MAGIC_GEN5:
        return read<NV_MULTI_CONTROLLER_PACKET>(bytes, [](auto &p) {
          return gamepad_state_t {static_cast<std::uint16_t>(util::endian::little(p.activeGamepadMask)), {static_cast<std::uint16_t>(util::endian::little(p.buttonFlags)) | (static_cast<std::uint32_t>(static_cast<std::uint16_t>(util::endian::little(p.buttonFlags2))) << 16), p.leftTrigger, p.rightTrigger, util::endian::little(p.leftStickX), util::endian::little(p.leftStickY), util::endian::little(p.rightStickX), util::endian::little(p.rightStickY)}};
        });
      case SS_CONTROLLER_ARRIVAL_MAGIC:
        return read<SS_CONTROLLER_ARRIVAL_PACKET>(bytes, [](auto &p) {
          return platf::gamepad_arrival_t {p.type, util::endian::little(p.capabilities), util::endian::little(p.supportedButtonFlags)};
        });
      case SS_CONTROLLER_TOUCH_MAGIC:
        return read<SS_CONTROLLER_TOUCH_PACKET>(bytes, [](auto &p) {
          return platf::gamepad_touch_t {{p.controllerNumber}, p.eventType, util::endian::little(p.pointerId), unit_float(p.x), unit_float(p.y), unit_float(p.pressure)};
        });
      case SS_CONTROLLER_MOTION_MAGIC:
        return read<SS_CONTROLLER_MOTION_PACKET>(bytes, [](auto &p) {
          return platf::gamepad_motion_t {{p.controllerNumber}, p.motionType, net_float(p.x), net_float(p.y), net_float(p.z)};
        });
      case SS_CONTROLLER_BATTERY_MAGIC:
        return read<SS_CONTROLLER_BATTERY_PACKET>(bytes, [](auto &p) {
          return platf::gamepad_battery_t {{p.controllerNumber}, p.batteryState, p.batteryPercentage};
        });
      default:
        return std::nullopt;
    }
  }

  std::shared_ptr<input_t> alloc(safe::mail_t mail) {
    return std::make_shared<input_t>(input_t {mail->queue<gamepad_event_t>("gamepad_input")});
  }

  void passthrough(std::shared_ptr<input_t> &input, std::vector<std::uint8_t> &&bytes) {
    if (input) {
      if (auto event = decode(bytes)) {
        input->events->raise(std::move(*event));
      }
    }
  }

  void reset(std::shared_ptr<input_t> &input) {
    if (input) {
      input->events->stop();
    }
    input.reset();
  }
}  // namespace input
