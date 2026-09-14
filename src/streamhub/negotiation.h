/**
 * @file src/streamhub/negotiation.h
 * @brief Exact conversion of Moonlight requirements to public protocol values.
 */
#pragma once
#include <streamhub-protocal/protocol.hpp>
#include <string>

namespace streamhub {
  /** @brief Values negotiated by RTSP, without Sunshine runtime dependencies. */
  struct requirements {
    int width = 1920, height = 1080;  ///< Requested visible dimensions.
    int fps = 60, fps_x100 = 0;  ///< Integer or optional display refresh rate.
    int bitrate_kbps = 10000;  ///< Video bandwidth after transport overhead.
    int codec = 0, csc = 2;  ///< Moonlight codec and colorspace/range values.
    int depth = 0, chroma = 0, intra_refresh = 0;  ///< Unsupported extensions must remain zero.
    int references = 0, slices = 1;  ///< Exact decoder constraints.
    int channels = 2, channel_mask = 3, packet_ms = 5;  ///< PCM layout and Opus duration.
  };

  /** @brief Build a CONNECT_REQUEST or throw for unrepresentable requirements. */
  protocal::connect_request_info negotiate(const std::string &input, const requirements &client);
  /** @brief Reject any ACCEPT that changes required media or exceeds optional capability limits. */
  void validate_accept(const protocal::connect_request_info &request, const protocal::connect_accept_info &accepted);
}  // namespace streamhub
