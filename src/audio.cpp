/**
 * @file src/audio.cpp
 * @brief Definitions for PCM audio encoding.
 */
// standard includes
#include <thread>

// lib includes
#include <opus/opus_multistream.h>

// local includes
#include "audio.h"
#include "globals.h"
#include "logging.h"
#include "platform/common.h"
#include "thread_safe.h"
#include "utility.h"

namespace audio {
  using namespace std::literals;
  /**
   * @brief Owning pointer for an Opus multistream encoder.
   */
  using opus_t = util::safe_ptr<OpusMSEncoder, opus_multistream_encoder_destroy>;
  /**
   * @brief Shared queue carrying PCM sample buffers to the encoder thread.
   */

  static void apply_surround_params(opus_stream_config_t &stream, const stream_params_t &params);

  /**
   * @brief Select the Opus stream configuration for a channel count and quality tier.
   *
   * @param channels Number of audio channels in the stream.
   * @param quality Whether the high-quality Opus layout should be selected.
   * @return Index into `stream_configs` for the requested layout.
   */
  int map_stream(int channels, bool quality);

  constexpr auto SAMPLE_RATE = 48000;  ///< Audio sample rate in hertz required by Opus.

  // NOTE: If you adjust the bitrates listed here, make sure to update the
  // corresponding bitrate adjustment logic in rtsp_stream::cmd_announce()
  /**
   * @brief Opus stream layouts and bitrates advertised to clients.
   */
  opus_stream_config_t stream_configs[MAX_STREAM_CONFIG] {
    {
      SAMPLE_RATE,
      2,
      1,
      1,
      platf::speaker::map_stereo.data(),
      96000,
    },
    {
      SAMPLE_RATE,
      2,
      1,
      1,
      platf::speaker::map_stereo.data(),
      512000,
    },
    {
      SAMPLE_RATE,
      6,
      4,
      2,
      platf::speaker::map_surround51.data(),
      256000,
    },
    {
      SAMPLE_RATE,
      6,
      6,
      0,
      platf::speaker::map_surround51.data(),
      1536000,
    },
    {
      SAMPLE_RATE,
      8,
      5,
      3,
      platf::speaker::map_surround71.data(),
      450000,
    },
    {
      SAMPLE_RATE,
      8,
      8,
      0,
      platf::speaker::map_surround71.data(),
      2048000,
    },
  };

  /**
   * @brief Encode PCM samples into Opus packets on the audio worker thread.
   *
   * @param samples Queue of PCM sample buffers to encode.
   * @param config Audio stream settings negotiated with the client.
   * @param channel_data Platform-specific audio capture context passed to packet metadata.
   */
  void encodeThread(sample_queue_t samples, config_t config, void *channel_data) {
    auto packets = mail::man->queue<packet_t>(mail::audio_packets);
    try {
      pcm_encoder encoder(config);
      while (auto sample = samples->pop()) {
        packets->raise(channel_data, encoder.encode(*sample));
      }
    } catch (const std::exception &e) {
      BOOST_LOG(error) << "Opus session failed: " << e.what();
    }
  }

  /** @brief Native state for one negotiated PCM encoder. */
  struct pcm_encoder::state {
    opus_t opus;  ///< Native codec.
    size_t samples;  ///< Scalar samples per block.
    int frames;  ///< Per-channel frames per block.
  };

  pcm_encoder::pcm_encoder(config_t config):
      state_(std::make_unique<state>()) {
    auto stream = stream_configs[map_stream(config.channels, config.flags[config_t::HIGH_QUALITY])];
    if (config.flags[config_t::CUSTOM_SURROUND_PARAMS]) {
      apply_surround_params(stream, config.customStreamParams);
    }
    state_->frames = config.packetDuration * stream.sampleRate / 1000;
    state_->samples = size_t(state_->frames) * stream.channelCount;
    if (state_->frames <= 0 || (stream.channelCount != 2 && stream.channelCount != 6 && stream.channelCount != 8)) {
      throw std::invalid_argument("invalid Opus block or channels");
    }
    int error = OPUS_OK;
    state_->opus = opus_t(opus_multistream_encoder_create(stream.sampleRate, stream.channelCount, stream.streams, stream.coupledStreams, stream.mapping, OPUS_APPLICATION_RESTRICTED_LOWDELAY, &error));
    if (!state_->opus || error != OPUS_OK) {
      throw std::runtime_error("could not initialize Opus");
    }
    if (opus_multistream_encoder_ctl(state_->opus.get(), OPUS_SET_BITRATE(stream.bitrate)) != OPUS_OK || opus_multistream_encoder_ctl(state_->opus.get(), OPUS_SET_VBR(0)) != OPUS_OK) {
      throw std::runtime_error("could not configure Opus");
    }
  }

  pcm_encoder::~pcm_encoder() = default;

  void pcm_encoder::reset() {
    if (opus_multistream_encoder_ctl(state_->opus.get(), OPUS_RESET_STATE) != OPUS_OK) {
      throw std::runtime_error("could not reset Opus");
    }
  }

  buffer_t pcm_encoder::encode(const std::vector<float> &samples) {
    if (samples.size() != state_->samples) {
      throw std::invalid_argument("PCM block does not match Opus layout");
    }
    buffer_t packet {max_packet_bytes};
    auto bytes = opus_multistream_encode_float(state_->opus.get(), samples.data(), state_->frames, packet.begin(), packet.size());
    if (bytes < 0) {
      throw std::runtime_error(opus_strerror(bytes));
    }
    packet.fake_resize(bytes);
    return packet;
  }

  /**
   * @brief Select the Opus stream configuration for a channel count and quality tier.
   */
  int map_stream(int channels, bool quality) {
    int shift = quality ? 1 : 0;
    switch (channels) {
      case 2:
        return STEREO + shift;
      case 6:
        return SURROUND51 + shift;
      case 8:
        return SURROUND71 + shift;
    }
    return STEREO;
  }

  void apply_surround_params(opus_stream_config_t &stream, const stream_params_t &params) {
    stream.channelCount = params.channelCount;
    stream.streams = params.streams;
    stream.coupledStreams = params.coupledStreams;
    stream.mapping = params.mapping;
  }
}  // namespace audio
