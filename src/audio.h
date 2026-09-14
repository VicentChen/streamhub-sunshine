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
  inline constexpr size_t max_datagram_bytes = 1400;  ///< Moonlight AudioStream.c receive buffer, including RTP/FEC headers.
  inline constexpr size_t max_packet_bytes = 1360;  ///< Leaves 24 bytes for RTP/FEC and up to 16 bytes for CBC padding.
  inline constexpr size_t max_encrypted_packet_bytes = (max_packet_bytes / 16 + 1) * 16;  ///< PKCS#7 adds a full block for aligned plaintext.

  /**
   * @brief Reject audio configurations that cannot fit Moonlight's receive buffer.
   * @param config Negotiated channel count, quality and packet duration.
   * @throws std::invalid_argument For unsupported formats or oversized CBR packets.
   */
  void validate_config(const config_t &config);

  /**
   * @brief Encoded audio packet paired with platform channel metadata.
   */
  struct packet_t {
    void *first;  ///< Session routing pointer.
    buffer_t second;  ///< Encoded Opus payload.
    std::shared_ptr<void> lifetime;  ///< Session lifetime until network delivery finishes.
    std::optional<uint32_t> timestamp;  ///< Source-derived RTP timestamp in milliseconds.

    /** @brief Construct an encoded packet using the retained legacy arguments. */
    packet_t(void *channel, buffer_t bytes):
        first(channel),
        second(std::move(bytes)) {}
  };

  /** @brief Session-owned Opus encoder; failures propagate only to its calling session. */
  class pcm_encoder {
  public:
    /** @brief Construct an encoder matching the negotiated Moonlight layout. */
    explicit pcm_encoder(config_t config);
    /** @brief Release the native Opus encoder. */
    ~pcm_encoder();
    /** @brief Encode one complete interleaved PCM block. */
    buffer_t encode(const std::vector<float> &samples);
    /** @brief Resynchronize codec history after an explicit source discontinuity. */
    void reset();

  private:
    struct state;
    std::unique_ptr<state> state_;  ///< Private Opus state.
  };

  /** @brief Queue of interleaved float PCM frames supplied to the Opus encoder. */
  using sample_queue_t = std::shared_ptr<safe::queue_t<std::vector<float>>>;
  /** @brief Encode PCM frames until the queue stops. */
  void encodeThread(sample_queue_t samples, config_t config, void *channel_data);
}  // namespace audio
