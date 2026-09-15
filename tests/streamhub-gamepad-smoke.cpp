/**
 * @file tests/streamhub-gamepad-smoke.cpp
 * @brief Exercise the production Sunshine gamepad bridge against a real Provider.
 */
#include "src/streamhub/gamepad.h"

#include <iostream>
#include <thread>

/** @brief Send Moonlight-format controller states using the production adapter. */
int main(int argc, char **argv) {
  try {
    if (argc != 2) {
      throw std::invalid_argument("usage: streamhub-gamepad-smoke SOCKET");
    }
    streamhub::requirements requirements;
    requirements.references = 1;
    streamhub::receiver receiver(argv[1], streamhub::negotiate("hdmi-main", requirements));
    const auto service = receiver.accepted().gamepad;
    if (service.max_controllers != 16 || service.input_features != 1 || service.feedback_features) {
      throw std::runtime_error("unexpected Provider gamepad intersection");
    }
    streamhub::gamepad_bridge bridge(receiver.memory(), service);
    // Allow the UI to complete its first frame; media queues then remain full.
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    bridge.arrival(0, 0x3fffff, true);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    auto send = [&](uint32_t buttons, uint16_t mask = 1) {
      while (!bridge.ready()) {
        bridge.flush();
        if (!receiver.poll() || std::chrono::steady_clock::now() >= deadline) {
          throw std::runtime_error("gamepad input stalled under media backpressure");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      bridge.state(0, mask, {buttons, 0, 0, 0, 0, 0, 0});
      bridge.flush();
    };
    for (unsigned i = 0; i < 2000; ++i) {
      send(0x2);  // Moonlight D-pad down; conversion occurs inside Sunshine.
      send(0);
    }
    send(0, 0);  // Explicit disconnect through activeGamepadMask reconciliation.
    for (unsigned i = 0; i < 150; ++i) {
      bridge.flush();
      if (!receiver.poll()) {
        throw std::runtime_error("Provider stopped before cleanup");
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    receiver.stop();
    std::cout << "PASS Sunshine bridge -> protocol -> real Provider: 2000 short presses with full media queues\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
