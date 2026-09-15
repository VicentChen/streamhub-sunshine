/**
 * @file src/streamhub/catalog.h
 * @brief Stable Provider input identities and continuously refreshed snapshots.
 */
#pragma once
#include "transport.h"

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace streamhub {
  /** @brief A selectable Moonlight application derived from one opaque Provider input. */
  struct catalog_entry {
    int app_id;  ///< Persistent positive identity.
    protocal::input_info input;  ///< Current display name and signal state.
  };

  /** @brief Thread-safe catalog, independent from legacy apps.json metadata. */
  class catalog {
  public:
    /** @brief Load Receiver-owned identity mappings; empty filename disables persistence for tests. */
    explicit catalog(std::string identity_file = {});
    /** @brief Stop the subscription worker. */
    ~catalog();
    /** @brief Replace a complete snapshot, preserving identities through reorder/removal. */
    void update(const protocal::input_list_info &list);
    /** @brief Record Provider disconnection while preserving the last known directory. */
    void offline();
    /** @brief Return a copy of current entries. */
    std::vector<catalog_entry> entries() const;
    /** @brief Return a currently selectable input or throw for offline/removed inputs. */
    std::string select(int app_id) const;
    /** @brief Return whether a valid Provider snapshot is currently connected. */
    bool online() const;
    /** @brief Start a cancellable subscription with bounded reconnect intervals. */
    void start(std::string socket);

  private:
    /** @brief Read and subscribe until cancelled; retry disconnected Providers. */
    void run(std::stop_token stop, const std::string &socket);
    mutable std::mutex mutex_;  ///< Guards entries, identities and online state.
    std::map<std::string, int> identities_;  ///< Never reuse retired input identities.
    std::vector<catalog_entry> entries_;  ///< Most recent complete input list.
    std::string file_;  ///< Receiver-owned persistent registry.
    bool online_ = false;  ///< A connected Provider supplied the snapshot.
    std::jthread thread_;  ///< Sole control subscriber.
  };

  inline std::shared_ptr<catalog> inputs;  ///< Runtime directory; null when Provider integration is disabled.
}  // namespace streamhub
