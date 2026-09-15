/**
 * @file src/streamhub/media.cpp
 * @brief DMA read completion, Annex-B checks and PCM copies.
 */
#include "media.h"

#include <cmath>
#include <cstring>
#include <stdexcept>

namespace streamhub {
  namespace p = protocal;

  namespace {
    /** @brief Reject a malformed shared descriptor or payload. */
    void require(bool value, const char *reason) {
      if (!value) {
        throw std::runtime_error(reason);
      }
    }

    /** @brief Check random-access parameter-set and frame-type claims without copying an access unit. */
    void inspect(std::span<const uint8_t> bytes, p::video_codec codec, bool idr, uint16_t expected_slices) {
      bool vcl = false, actual_idr = false, sps = false, pps = false, vps = false;
      unsigned slices = 0, picture_type = 0, temporal_id = 0;
      size_t first = SIZE_MAX;
      for (size_t i = 0; i + 3 < bytes.size(); ++i) {
        if (bytes[i] || bytes[i + 1]) {
          continue;
        }
        size_t header = 0;
        if (bytes[i + 2] == 1) {
          header = i + 3;
        } else if (i + 4 < bytes.size() && !bytes[i + 2] && bytes[i + 3] == 1) {
          header = i + 4;
        }
        if (!header) {
          continue;
        }
        if (first == SIZE_MAX) {
          first = i;
        }
        require(!(bytes[header] & 0x80), "StreamHub forbidden NAL bit");
        if (codec == p::video_codec::h264) {
          const unsigned type = bytes[header] & 31;
          if (type == 1 || type == 5) {
            require(!slices || picture_type == type, "StreamHub mixed H.264 slice types");
            require(type != 5 || (sps && pps), "StreamHub IDR missing preceding parameter sets");
            picture_type = type;
            ++slices;
          }
          vcl |= type == 1 || type == 5;
          actual_idr |= type == 5;
          sps |= type == 7;
          pps |= type == 8;
        } else {
          require(header + 1 < bytes.size() && (bytes[header + 1] & 7), "StreamHub invalid HEVC header");
          const unsigned type = (bytes[header] >> 1) & 63;
          if (type <= 31) {
            require(header + 2 < bytes.size(), "StreamHub truncated HEVC slice");
            require(unsigned(bytes[header + 2] >> 7) == unsigned(slices == 0), "StreamHub multiple HEVC pictures or missing first slice");
            require(!slices || (picture_type == type && temporal_id == (bytes[header + 1] & 7)), "StreamHub mixed HEVC picture prefixes");
            require(type < 16 || type == 19 || type == 20 || type > 23, "StreamHub unsupported non-IDR recovery");
            require((type != 19 && type != 20) || (vps && sps && pps), "StreamHub IDR missing preceding parameter sets");
            picture_type = type;
            temporal_id = bytes[header + 1] & 7;
            ++slices;
          }
          vcl |= type <= 31;
          actual_idr |= type == 19 || type == 20;
          vps |= type == 32;
          sps |= type == 33;
          pps |= type == 34;
        }
        i = header;
      }
      require(first == 0 && vcl && actual_idr == idr, "StreamHub Annex-B or IDR flag mismatch");
      require(slices == expected_slices, "StreamHub slice count differs from ACCEPT");
      require(!idr || (sps && pps && (codec == p::video_codec::h264 || vps)), "StreamHub IDR missing parameter sets");
    }
  }  // namespace

  video_lease::video_lease(std::shared_ptr<read_completion> c, std::shared_ptr<mapped_resource> m):
      completion_(std::move(c)),
      mapping_(std::move(m)) {}

  video_lease::~video_lease() {
    complete();
  }

  void video_lease::complete() {
    completion_->done.store(true, std::memory_order_release);
  }

  video_reader::video_reader(std::shared_ptr<resources> resources, p::video_codec codec, uint16_t slices):
      resources_(std::move(resources)),
      queue_(*static_cast<p::video_queue *>(resources_->at(0)->data)),
      codec_(codec),
      slices_(slices) {}

  bool video_reader::reclaim() {
    if (!pending_) {
      return true;
    }
    if (!pending_->done.load(std::memory_order_acquire)) {
      return false;
    }
    resources_->sync(slot_, false);
    queue_.dequeue();
    ++ordinal_;
    pending_.reset();
    return true;
  }

  std::optional<video_frame> video_reader::next() {
    if (!reclaim() || queue_.empty()) {
      return {};
    }
    const auto info = queue_.front();
    bool idr = info.flags & p::video_frame_flags::idr;
    require(!info.reserved && !(info.flags & ~3u) && (!(info.flags & p::video_frame_flags::discontinuity) || idr), "StreamHub invalid video flags");
    require(info.buffer_slot == ordinal_ % p::video_capacity && info.data_bytes && info.data_bytes <= resources_->at(4 + info.buffer_slot)->bytes, "StreamHub video slot or length mismatch");
    require(ordinal_ ? info.frame_id > last_id_ && info.pts_ns > last_pts_ : idr, "StreamHub invalid first frame or video timeline");
    const auto now = monotonic_ns();
    require(info.pts_ns && info.pts_ns <= now + 1000000000ULL && (!info.capture_time_ns || info.capture_time_ns <= now + 1000000000ULL), "StreamHub invalid video clock");
    auto mapping = resources_->at(4 + info.buffer_slot);
    // Allocate notification before START so allocation failure leaves no active read.
    auto completion = std::make_shared<read_completion>();
    auto lease = std::make_shared<video_lease>(completion, mapping);
    resources_->sync(info.buffer_slot, true);
    auto bytes = std::span(static_cast<const uint8_t *>(mapping->data), info.data_bytes);
    try {
      inspect(bytes, codec_, idr, slices_);
    } catch (...) {
      resources_->sync(info.buffer_slot, false);
      throw;
    }
    slot_ = info.buffer_slot;
    pending_ = std::move(completion);
    last_id_ = info.frame_id;
    last_pts_ = info.pts_ns;
    return video_frame {info, ordinal_ + 1, bytes, std::move(lease)};
  }

  audio_reader::audio_reader(std::shared_ptr<resources> resources, p::audio_request_info format):
      resources_(std::move(resources)),
      queue_(*static_cast<p::audio_queue *>(resources_->at(1)->data)),
      format_(format) {}

  std::optional<pcm_frame> audio_reader::next() {
    if (queue_.empty()) {
      return {};
    }
    const auto info = queue_.front();
    require(!info.reserved && !(info.flags & ~1u) && info.buffer_slot == ordinal_ % p::audio_capacity, "StreamHub invalid PCM descriptor");
    require(info.pts_ns && info.pts_ns <= monotonic_ns() + 1000000000ULL, "StreamHub invalid PCM clock");
    if (ordinal_) {
      require(last_sample_ <= UINT64_MAX - format_.block_frames && info.sample_index >= last_sample_ + format_.block_frames && info.pts_ns > last_pts_, "StreamHub nonmonotonic PCM");
      const auto gap = info.sample_index - last_sample_;
      require(gap <= UINT64_MAX / 1000000000ULL, "StreamHub PCM timeline overflow");
      auto expected = gap * 1000000000ULL / format_.format.sample_rate;
      auto elapsed = info.pts_ns - last_pts_;
      auto difference = elapsed > expected ? elapsed - expected : expected - elapsed;
      require(difference <= 1000000, "StreamHub inconsistent PCM sample/PTS timeline");
      require(gap == format_.block_frames || (info.flags & p::audio_frame_flags::discontinuity), "StreamHub missing PCM discontinuity flag");
    }
    size_t channels = format_.format.channel_layout == p::audio_channel_layout::stereo ? 2 : format_.format.channel_layout == p::audio_channel_layout::surround_5_1 ? 6 :
                                                                                                                                                                      8;
    size_t count = size_t(format_.block_frames) * channels;
    size_t bytes = count * (format_.format.sample_format == p::audio_sample_format::f32_le ? 4 : 2);
    const auto *source = static_cast<const std::byte *>(resources_->at(12)->data) + info.buffer_slot * bytes;
    pcm_frame result {std::vector<float>(count), info.sample_index, info.pts_ns, bool(info.flags & p::audio_frame_flags::discontinuity)};
    if (format_.format.sample_format == p::audio_sample_format::f32_le) {
      std::memcpy(result.samples.data(), source, bytes);
    } else {
      for (size_t i = 0; i < count; ++i) {
        int16_t value;
        std::memcpy(&value, source + 2 * i, 2);
        result.samples[i] = value / 32768.f;
      }
    }
    for (float value : result.samples) {
      require(std::isfinite(value) && value >= -1 && value <= 1, "StreamHub invalid float PCM sample");
    }
    last_sample_ = info.sample_index;
    last_pts_ = info.pts_ns;
    ++ordinal_;
    queue_.dequeue();
    return result;
  }
}  // namespace streamhub
