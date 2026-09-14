/**
 * @file src/streamhub/catalog.cpp
 * @brief Receiver input directory and persistent collision resolution.
 */
#include "catalog.h"

#include <algorithm>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <set>
#include <stdexcept>

namespace streamhub {
  using namespace std::chrono_literals;

  catalog::catalog(std::string file):
      file_(std::move(file)) {
    if (file_.empty() || !std::filesystem::exists(file_)) {
      return;
    }
    std::ifstream in(file_);
    std::string key;
    int value;
    std::set<int> used;
    while (in >> std::quoted(key) >> value) {
      if (key.empty() || key.size() > protocal::max_input_id_bytes || value <= 0 || identities_.contains(key) || !used.insert(value).second) {
        throw std::runtime_error("StreamHub: invalid input identity registry");
      }
      identities_.emplace(key, value);
    }
    if (!in.eof()) {
      throw std::runtime_error("StreamHub: unreadable input identity registry");
    }
  }

  catalog::~catalog() = default;

  void catalog::update(const protocal::input_list_info &list) {
    std::lock_guard guard(mutex_);
    auto identities = identities_;
    auto sorted = list.inputs;
    std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) {
      return a.input_id < b.input_id;
    });
    std::set<int> used;
    for (const auto &[key, id] : identities) {
      used.insert(id);
    }
    for (const auto &input : sorted) {
      if (identities.contains(input.input_id)) {
        continue;
      }
      uint32_t hash = 2166136261u;
      for (unsigned char c : input.input_id) {
        hash = (hash ^ c) * 16777619u;
      }
      int id = (hash & 0x7fffffff) ? int(hash & 0x7fffffff) : 1;
      while (used.contains(id)) {
        id = id == INT32_MAX ? 1 : id + 1;
      }
      identities[input.input_id] = id;
      used.insert(id);
    }
    if (!file_.empty() && identities != identities_) {
      auto temp = file_ + ".tmp";
      {
        std::ofstream out(temp, std::ios::trunc);
        for (const auto &[key, id] : identities) {
          out << std::quoted(key) << ' ' << id << '\n';
        }
        out.close();
        if (!out) {
          throw std::runtime_error("StreamHub: cannot persist input identities");
        }
      }
      std::filesystem::rename(temp, file_);
    }
    std::vector<catalog_entry> entries;
    for (const auto &input : list.inputs) {
      entries.push_back({identities.at(input.input_id), input});
    }
    identities_ = std::move(identities);
    entries_ = std::move(entries);
    online_ = true;
  }

  void catalog::offline() {
    std::lock_guard guard(mutex_);
    online_ = false;
  }

  bool catalog::online() const {
    std::lock_guard guard(mutex_);
    return online_;
  }

  std::vector<catalog_entry> catalog::entries() const {
    std::lock_guard guard(mutex_);
    return entries_;
  }

  std::string catalog::select(int app_id) const {
    std::lock_guard guard(mutex_);
    if (!online_) {
      throw std::runtime_error("StreamHub Provider is offline");
    }
    for (const auto &entry : entries_) {
      if (entry.app_id == app_id) {
        return entry.input.input_id;
      }
    }
    throw std::runtime_error("StreamHub input was removed or is unknown");
  }

  void catalog::start(std::string socket) {
    thread_ = std::jthread([this, socket = std::move(socket)](std::stop_token stop) {
      run(stop, socket);
    });
  }

  void catalog::run(std::stop_token stop, const std::string &socket) {
    std::mutex wait_mutex;
    std::condition_variable_any changed;
    while (!stop.stop_requested()) {
      try {
        transport connection(socket, std::chrono::steady_clock::now() + 2s, stop);
        connection.send({{protocal::protocol_major, protocal::protocol_minor, protocal::control_message_type::get_inputs, 0, 0, 1, 0}, {}}, std::chrono::steady_clock::now() + 2s, stop);
        bool first = true;
        while (!stop.stop_requested()) {
          auto packet = connection.receive(std::chrono::steady_clock::now() + (first ? 2s : 24h), stop);
          const auto &m = packet.message;
          if (m.header.message_type != protocal::control_message_type::input_list || m.header.session_id || m.header.request_id != (first ? 1u : 0u)) {
            throw std::runtime_error("StreamHub: unexpected directory response");
          }
          update(std::get<protocal::input_list_info>(m.body));
          first = false;
        }
      } catch (const std::exception &) {
        offline();
      }
      std::unique_lock guard(wait_mutex);
      changed.wait_for(guard, stop, 250ms, [] {
        return false;
      });
    }
  }
}  // namespace streamhub
