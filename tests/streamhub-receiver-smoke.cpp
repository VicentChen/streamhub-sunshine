/**
 * @file tests/streamhub-receiver-smoke.cpp
 * @brief Finite production Receiver handshake and DMA/PCM smoke test.
 */
#include "src/streamhub/media.h"

#include <iostream>
#include <thread>

int main(int argc, char **argv) {
  try {
    if (argc != 3) {
      throw std::invalid_argument("usage: streamhub-receiver-smoke SOCKET h264|hevc");
    }
    streamhub::requirements requirements;
    requirements.codec = std::string(argv[2]) == "hevc" ? 1 : 0;
    requirements.references = 1;
    requirements.fps_x100 = 5994;
    streamhub::receiver receiver(argv[1], streamhub::negotiate("hdmi-main", requirements));
    streamhub::video_reader video(receiver.memory(), receiver.accepted().video.format.codec);
    streamhub::audio_reader audio(receiver.memory(), receiver.accepted().audio);
    unsigned frames = 0, blocks = 0;
    bool refreshed = false;
    uint64_t hash = 0;
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (frames < 120 && std::chrono::steady_clock::now() < until) {
      if (!receiver.poll()) {
        throw std::runtime_error("Provider stopped early");
      }
      if (auto frame = video.next()) {
        for (uint8_t byte : frame->bytes) {
          hash = (hash * 131) ^ byte;
        }
        ++frames;
        if (frames == 10) {
          receiver.request_idr();
        }
        if (frames > 10 && (frame->info.flags & streamhub::protocal::video_frame_flags::idr)) {
          refreshed = true;
        }
        frame->lease->complete();
      }
      if (auto block = audio.next()) {
        ++blocks;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!video.reclaim() || frames != 120 || !blocks || !refreshed) {
      throw std::runtime_error("missing video, PCM or requested IDR");
    }
    auto id = receiver.id();
    receiver.stop();
    std::cout << argv[2] << " session=" << id << " frames=" << frames << " PCM=" << blocks << " IDR=ok hash=" << hash << "\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}
