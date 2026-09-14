/**
 * @file src/streamhub/media.h
 * @brief Consumer-owned DMA video leases and PCM timeline validation.
 */
#pragma once
#include "receiver.h"

#include <atomic>
#include <span>

namespace streamhub {
  /** @brief Completion state shared with a network packet; never touches queue indices. */
  struct read_completion {
    std::atomic_bool done {false};
  };

  /** @brief Downstream lease; completing it only notifies the original consumer. */
  class video_lease {
  public:
    /** @brief Keep the backing alive until the downstream read completes. */
    video_lease(std::shared_ptr<read_completion> completion, std::shared_ptr<mapped_resource> mapping);
    /** @brief Notify the original consumer even when a packet is dropped. */
    ~video_lease();
    /** @brief Indicate that no downstream code will read these bytes again. */
    void complete();

  private:
    std::shared_ptr<read_completion> completion_;  ///< Consumer notification.
    std::shared_ptr<mapped_resource> mapping_;  ///< Backing lifetime.
  };

  /** @brief One encoded frame with borrowed DMA bytes protected by an explicit lease. */
  struct video_frame {
    protocal::video_frame_info info;  ///< Immutable Provider metadata.
    uint64_t network_index;  ///< Sequential Moonlight frame number, starting at one.
    std::span<const uint8_t> bytes;  ///< Valid until lease completion.
    std::shared_ptr<video_lease> lease;  ///< Ownership for downstream reads.
  };

  /** @brief Single video consumer, allowing at most one downstream read at a time. */
  class video_reader {
  public:
    /** @brief Bind to validated shared queues and accepted codec. */
    video_reader(std::shared_ptr<resources> resources, protocal::video_codec codec);
    /** @brief Return a new frame if available; reclaim a completed old lease first. */
    std::optional<video_frame> next();
    /** @brief Finish READ END and dequeue when the downstream read has completed. */
    bool reclaim();

    /** @brief Return whether a downstream read remains in flight. */
    bool pending() const {
      return bool(pending_);
    }

  private:
    std::shared_ptr<resources> resources_;  ///< Keeps mappings valid.
    protocal::video_queue::consumer queue_;  ///< Sole tail writer.
    protocal::video_codec codec_;  ///< Expected Annex-B codec.
    std::shared_ptr<read_completion> pending_;  ///< One outstanding lease.
    uint64_t ordinal_ = 0, last_id_ = 0, last_pts_ = 0;  ///< Publication and time validation.
    size_t slot_ = 0;  ///< Slot awaiting READ END.
  };

  /** @brief Owned float PCM plus absolute source sample and presentation timestamps. */
  struct pcm_frame {
    std::vector<float> samples;  ///< Protocol channel order, copied before dequeue.
    uint64_t sample_index, pts_ns;  ///< Exact source timeline.
    bool discontinuity;  ///< Source explicitly reported a timeline break.
  };

  /** @brief Single PCM consumer; exact block copies leave no outstanding shared references. */
  class audio_reader {
  public:
    /** @brief Bind PCM queue and pool from a validated handshake. */
    audio_reader(std::shared_ptr<resources> resources, protocal::audio_request_info format);
    /** @brief Copy one block after validating slot, values and source timeline. */
    std::optional<pcm_frame> next();

  private:
    std::shared_ptr<resources> resources_;  ///< Mapping owner.
    protocal::audio_queue::consumer queue_;  ///< Sole audio tail writer.
    protocal::audio_request_info format_;  ///< Exact accepted PCM layout.
    uint64_t ordinal_ = 0, last_sample_ = 0, last_pts_ = 0;  ///< Publication/timeline validation.
  };
}  // namespace streamhub
