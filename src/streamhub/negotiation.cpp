/**
 * @file src/streamhub/negotiation.cpp
 * @brief Public protocol negotiation; no capture or encoder policy.
 */
#include "negotiation.h"

#include <limits>
#include <numeric>
#include <stdexcept>
#include <tuple>

namespace streamhub {
  namespace {
    /** @brief Throw a readable negotiation error for a false predicate. */
    void require(bool condition, const char *reason) {
      if (!condition) {
        throw std::invalid_argument(reason);
      }
    }

    /** @brief Compare all format fields without inspecting native padding. */
    auto fields(const protocal::video_format_info &f) {
      return std::tie(f.codec, f.profile, f.width, f.height, f.fps_num, f.fps_den, f.color, f.range);
    }
  }  // namespace

  protocal::connect_request_info negotiate(const std::string &input, const requirements &c) {
    namespace p = protocal;
    require(c.codec == 0 || c.codec == 1, "StreamHub: unsupported codec");
    require(!c.depth && !c.chroma && !c.intra_refresh, "StreamHub: HDR, 4:4:4 and intra refresh are unavailable");
    require(c.width > 0 && c.height > 0 && c.fps > 0 && c.fps_x100 >= 0, "StreamHub: invalid dimensions or framerate");
    require(c.bitrate_kbps > 0 && uint64_t(c.bitrate_kbps) * 1000 <= UINT32_MAX, "StreamHub: bitrate overflow");
    require(c.references >= 0 && c.references <= UINT16_MAX && c.slices > 0 && c.slices <= UINT16_MAX, "StreamHub: invalid decoder constraints");
    require(c.csc >= 0 && c.csc <= 3, "StreamHub: unsupported colorspace");
    require(c.channels == 2 || c.channels == 6 || c.channels == 8, "StreamHub: unsupported audio channels");
    require((c.channels == 2 && c.channel_mask == 3) || (c.channels == 6 && (c.channel_mask == 0x3f || c.channel_mask == 0x60f)) || (c.channels == 8 && c.channel_mask == 0x63f), "StreamHub: unsupported speaker mask");
    require(c.packet_ms == 5 || c.packet_ms == 10 || c.packet_ms == 20, "StreamHub: unsupported Opus duration");
    uint32_t num = c.fps, den = 1;
    if (c.fps_x100) {
      // RTSP expresses NTSC refresh rounded to two decimal places.
      if (uint64_t(c.fps) * 100000 / 1001 == uint32_t(c.fps_x100)) {
        num = uint32_t(c.fps) * 1000;
        den = 1001;
      } else {
        num = c.fps_x100;
        den = 100;
      }
      auto divisor = std::gcd(num, den);
      num /= divisor;
      den /= divisor;
    }
    p::connect_request_info result {};
    result.input_id = input;
    result.video = {{c.codec == 0 ? p::video_codec::h264 : p::video_codec::hevc, c.codec == 0 ? p::video_profile::h264_high : p::video_profile::hevc_main, uint32_t(c.width), uint32_t(c.height), num, den, (c.csc >> 1) == 1 ? p::video_color::bt709_sdr : p::video_color::bt601_525_sdr, (c.csc & 1) ? p::video_range::full : p::video_range::limited}, uint32_t(c.bitrate_kbps) * 1000, 0, uint16_t(c.references), uint16_t(c.slices)};
    result.audio = {{p::audio_sample_format::f32_le, 48000, c.channels == 2 ? p::audio_channel_layout::stereo : c.channels == 6 ? p::audio_channel_layout::surround_5_1 :
                                                                                                                                  p::audio_channel_layout::surround_7_1},
                    uint32_t(c.packet_ms) * 48};
    result.gamepad = {16, p::gamepad_input_features::basic_state, p::gamepad_feedback_features::rumble};
    result.queue_layout = p::queue_layout_v2;
    result.max_shared_bytes = 96ULL * 1024 * 1024;
    std::vector<std::byte> bytes;
    require(p::encode_control({{p::protocol_major, p::protocol_minor, p::control_message_type::connect_request, 0, 0, 1, 0}, result}, bytes) == p::control_error::none, "StreamHub: invalid protocol request");
    return result;
  }

  void validate_accept(const protocal::connect_request_info &r, const protocal::connect_accept_info &a) {
    require(fields(r.video.format) == fields(a.video.format) && r.video.bitrate_bps == a.video.bitrate_bps && r.video.slices_per_frame && a.video.slices_per_frame >= r.video.slices_per_frame && (!r.video.max_level || a.video.level <= r.video.max_level) && a.video.ref_frames && (!r.video.max_ref_frames || a.video.ref_frames <= r.video.max_ref_frames), "StreamHub: ACCEPT changed video requirements");
    require(r.audio.format.sample_format == a.audio.format.sample_format && r.audio.format.sample_rate == a.audio.format.sample_rate && r.audio.format.channel_layout == a.audio.format.channel_layout && r.audio.block_frames == a.audio.block_frames, "StreamHub: ACCEPT changed PCM requirements");
    require(r.queue_layout == a.queue_layout && a.queue_layout == protocal::queue_layout_v2, "StreamHub: incompatible queue ABI");
    require(a.gamepad.max_controllers <= r.gamepad.max_controllers && !(a.gamepad.input_features & ~r.gamepad.input_features) && !(a.gamepad.feedback_features & ~r.gamepad.feedback_features), "StreamHub: ACCEPT exceeds requested gamepad capabilities");
  }
}  // namespace streamhub
