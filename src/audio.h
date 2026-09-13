/**
 * @file src/audio.h
 * @brief Declarations for PCM audio encoding.
 */
#pragma once

// local includes
#include "platform/common.h"
#include "thread_safe.h"
#include "utility.h"

#include <bitset>

namespace audio {
  /**
   * @brief Supported Opus channel layouts advertised to Moonlight clients.
   */
  enum stream_config_e : int {
    STEREO,  ///< Stereo
    HIGH_STEREO,  ///< High stereo
    SURROUND51,  ///< Surround 5.1
    HIGH_SURROUND51,  ///< High surround 5.1
    SURROUND71,  ///< Surround 7.1
    HIGH_SURROUND71,  ///< High surround 7.1
    MAX_STREAM_CONFIG  ///< Maximum audio stream configuration
  };

  /**
   * @brief Static Opus encoder layout for one advertised audio mode.
   */
  struct opus_stream_config_t {
    std::int32_t sampleRate;  ///< Opus sample rate in hertz.
    int channelCount;  ///< Number of audio channels in the Opus layout.
    int streams;  ///< Number of Opus streams in the layout.
    int coupledStreams;  ///< Number of stereo-coupled Opus streams.
    const std::uint8_t *mapping;  ///< Channel mapping table passed to the Opus encoder.
    int bitrate;  ///< Target bitrate in bits per second.
  };

  /**
   * @brief Custom Opus channel layout supplied by configuration.
   */
  struct stream_params_t {
    int channelCount;  ///< Number of audio channels in the Opus layout.
    int streams;  ///< Number of Opus streams in the custom layout.
    int coupledStreams;  ///< Number of stereo-coupled Opus streams.
    std::uint8_t mapping[8];  ///< Channel mapping table for up to eight speakers.
  };

  extern opus_stream_config_t stream_configs[MAX_STREAM_CONFIG];

  /**
   * @brief Audio encoder settings for a stream.
   */
  struct config_t {
    /**
     * @brief Boolean audio feature flags.
     */
    enum flags_e : int {
      HIGH_QUALITY,  ///< High quality audio
      CUSTOM_SURROUND_PARAMS,  ///< Custom surround parameters
      MAX_FLAGS  ///< Maximum number of flags
    };

    int packetDuration;  ///< Packet duration in milliseconds requested by the client.
    int channels;  ///< Number of audio channels requested by the client.
    int mask;  ///< Speaker mask describing the requested channel layout.

    stream_params_t customStreamParams;  ///< Custom Opus layout used when CUSTOM_SURROUND_PARAMS is enabled.

    std::bitset<MAX_FLAGS> flags;  ///< Enabled audio feature flags.
  };

  /**
   * @brief Byte buffer used for encoded audio packet payloads.
   */
  using buffer_t = util::buffer_t<std::uint8_t>;
  /**
   * @brief Encoded audio packet paired with platform channel metadata.
   */
  using packet_t = std::pair<void *, buffer_t>;
  /** @brief Queue of interleaved float PCM frames supplied to the Opus encoder. */
  using sample_queue_t = std::shared_ptr<safe::queue_t<std::vector<float>>>;
  /** @brief Encode PCM frames until the queue stops. */
  void encodeThread(sample_queue_t samples, config_t config, void *channel_data);
}  // namespace audio
