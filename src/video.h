/**
 * @file src/video.h
 * @brief Negotiated video settings and encoded packets for network delivery.
 */
#pragma once
#include "utility.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <moonlight-common-c/src/Limelight.h>
#include <optional>
#include <string_view>
#include <vector>

namespace video {
  struct config_t {
    int width;  ///< Video width in pixels.
    int height;  ///< Video height in pixels.
    int framerate;  ///< Requested framerate used in the per-frame bitrate budget.
    int framerateX100;  ///< Optional NTSC-style framerate value, e.g. 59.94 as 5994.
    int bitrate;  ///< Video bitrate in kilobits for the requested framerate.
    int slicesPerFrame;  ///< Number of slices per frame.
    int numRefFrames;  ///< Maximum number of reference frames.
    int encoderCscMode;  ///< Requested color range and SDR colorspace; HDR always uses BT.2020 and ST2084.
    int videoFormat;  ///< Video codec format: 0 = H.264, 1 = HEVC, 2 = AV1.
    int dynamicRange;  ///< Encoding color depth: 0 = 8-bit, 1 = 10-bit.
    int chromaSamplingType;  ///< Chroma sampling type: 0 = 4:2:0, 1 = 4:4:4.
    int enableIntraRefresh;  ///< Intra refresh setting: 0 = disabled, 1 = enabled.
  };

  struct packet_raw_t {
    virtual ~packet_raw_t() = default;

    /**
     * @brief Report whether this packet starts an IDR frame.
     *
     * @return True when this frame is an IDR frame.
     */
    virtual bool is_idr() = 0;

    /**
     * @brief Return the frame index associated with this encoded packet.
     *
     * @return Monotonic frame index assigned to this frame.
     */
    virtual int64_t frame_index() = 0;

    /**
     * @brief Return writable access to the encoded packet bytes.
     *
     * @return Pointer to the first byte of encoded frame data.
     */
    virtual uint8_t *data() = 0;

    /**
     * @brief Return the encoded payload length.
     *
     * @return Size of the encoded frame payload in bytes.
     */
    virtual size_t data_size() = 0;

    void *channel_data = nullptr;  ///< Platform or protocol state carried with this packet.
    bool after_ref_frame_invalidation = false;  ///< Whether the frame follows reference-frame invalidation.
    std::optional<std::chrono::steady_clock::time_point> frame_timestamp;  ///< Capture timestamp associated with the frame.
  };

  struct packet_raw_generic: packet_raw_t {
    /**
     * @brief Wrap generic encoded frame bytes in Sunshine packet metadata.
     *
     * @param frame_data Final encoded access-unit bytes.
     * @param frame_index Monotonic frame index assigned to the encoded frame.
     * @param idr Whether the packet begins an IDR frame.
     */
    packet_raw_generic(std::vector<uint8_t> &&frame_data, int64_t frame_index, bool idr):
        frame_data {std::move(frame_data)},
        index {frame_index},
        idr {idr} {
    }

    /**
     * @brief Report whether the generic packet starts an IDR frame.
     *
     * @return True when this frame is an IDR frame.
     */
    bool is_idr() override {
      return idr;
    }

    /**
     * @brief Return the stored frame index for this generic packet.
     *
     * @return Monotonic frame index assigned to this frame.
     */
    int64_t frame_index() override {
      return index;
    }

    /**
     * @brief Return writable access to the generic packet payload.
     *
     * @return Pointer to the first encoded byte in `frame_data`.
     */
    uint8_t *data() override {
      return frame_data.data();
    }

    /**
     * @brief Return the generic packet payload length.
     *
     * @return Size of the encoded frame payload in bytes.
     */
    size_t data_size() override {
      return frame_data.size();
    }

    std::vector<uint8_t> frame_data;  ///< Encoded frame bytes owned by this packet.
    int64_t index;  ///< Monotonic frame index assigned to this packet.
    bool idr;  ///< Whether the packet belongs to an IDR frame.
  };

  struct hdr_info_raw_t {
    /**
     * @brief Initialize HDR metadata with only the enabled state.
     *
     * @param enabled Whether the feature should be enabled.
     */
    explicit hdr_info_raw_t(bool enabled):
        enabled {enabled},
        metadata {} {};
    /**
     * @brief Initialize HDR metadata with display metadata from the backend.
     *
     * @param enabled Whether the feature should be enabled.
     * @param metadata Output structure populated with HDR metadata.
     */
    explicit hdr_info_raw_t(bool enabled, const SS_HDR_METADATA &metadata):
        enabled {enabled},
        metadata {metadata} {};

    bool enabled;  ///< Whether HDR mode should be enabled.
    SS_HDR_METADATA metadata;  ///< Display HDR metadata forwarded to the encoder and client.
  };

  using packet_t = std::unique_ptr<packet_raw_t>;
  using hdr_info_t = std::unique_ptr<hdr_info_raw_t>;

  // No media source is connected in the slimmed core yet. Capability flags must
  // describe that source, never the encoders installed on the local machine.
  inline std::uint32_t codec_mode_flags = 0;
  inline bool supports_ref_frames_invalidation = false;
}  // namespace video
