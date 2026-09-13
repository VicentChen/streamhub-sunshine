/**
 * @file src/config.h
 * @brief Declarations for the configuration of Sunshine.
 */
#pragma once

// standard includes
#include <bitset>
#include <chrono>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// local includes
#include <array>

namespace config {
  // Valid range for the packetsize limit
  constexpr int PACKETSIZE_MIN = 200;  ///< Lowest accepted configured packet size in bytes.
  constexpr int PACKETSIZE_MAX = 65535;  ///< Highest accepted configured packet size in bytes.
  constexpr int PACKETSIZE_SMALL = 500;  ///< Conservative packet size used for low-MTU links.
  constexpr int PACKETSIZE_LARGE = 1456;  ///< Default large packet size that avoids common MTU fragmentation.

  // track modified config options
  inline std::unordered_map<std::string, std::string> modified_config_settings;  ///< Configuration keys changed during the current parse or UI update.

  // sensitive values that should be redacted from logging
  /**
   * @brief Configuration keys whose values must be hidden in logs.
   */
  inline constexpr std::array redacted_config = {
    "csrf_allowed_origins"
  };

  /**
   * @brief Log configuration entries and optionally mark them for persistence.
   *
   * @param vars Parsed configuration entries to log.
   * @param save Whether modified configuration values should be written back to disk.
   */
  void log_config_settings(const std::unordered_map<std::string, std::string> &vars, bool save);

  /**
   * @brief Video encoder, capture, and color settings loaded from configuration.
   */
  struct video_t {
    int max_bitrate;
  };

  /**
   * @brief Audio capture and encoder settings loaded from configuration.
   */

  /**
   * @brief Encryption policy that always sends unencrypted video.
   */
  constexpr int ENCRYPTION_MODE_NEVER = 0;  // Never use video encryption, even if the client supports it
  /**
   * @brief Encryption policy that uses encrypted video only when the client supports it.
   */
  constexpr int ENCRYPTION_MODE_OPPORTUNISTIC = 1;  // Use video encryption if available, but stream without it if not supported
  /**
   * @brief Encryption policy that rejects clients without video encryption support.
   */
  constexpr int ENCRYPTION_MODE_MANDATORY = 2;  // Always use video encryption and refuse clients that can't encrypt

  /**
   * @brief Network stream settings shared by audio, video, and control channels.
   */
  struct stream_t {
    std::chrono::milliseconds ping_timeout;  ///< Timeout used when waiting for client ping responses.

    std::string file_apps;  ///< Path to the configured applications file.

    int fec_percentage;  ///< Percentage of forward-error-correction packets to add to the stream.

    // Video encryption settings for LAN and WAN streams
    int lan_encryption_mode;  ///< Video encryption policy for LAN clients.
    int wan_encryption_mode;  ///< Video encryption policy for WAN clients.

    // Limit the packetsize to avoid fragmentation on a low MTU link
    int packetsize;  ///< Maximum payload size for network packets.
  };

  /**
   * @brief HTTP and HTTPS settings used by the GameStream pairing server.
   */
  struct nvhttp_t {
    // Could be any of the following values:
    // pc|lan|wan
    std::string origin_web_ui_allowed;  ///< Origin policy used for Web UI access checks.

    std::string pkey;  ///< Private key PEM string or path.
    std::string cert;  ///< Certificate PEM string or path.

    std::string sunshine_name;  ///< Host name advertised to Moonlight clients.

    std::string file_state;  ///< Path to the persisted Sunshine state file.

    std::string external_ip;  ///< External address advertised to clients when configured.
  };

  /**
   * @brief Input emulation settings loaded from configuration.
   */

  namespace flag {
    /**
     * @brief Enumerates supported flag options.
     */
    enum flag_e : std::size_t {
      PIN_STDIN = 0,  ///< Read PIN from stdin instead of http
      FRESH_STATE,  ///< Do not load or save state
      UPNP,  ///< Try Universal Plug 'n Play
      CONST_PIN,  ///< Use "universal" pin
      FLAG_SIZE  ///< Number of flags
    };
  }  // namespace flag

  /**
   * @brief Top-level Sunshine configuration and credential state.
   */
  struct sunshine_t {
    std::string locale;  ///< Locale selected for Sunshine UI and log messages.
    int min_log_level;  ///< Minimum severity level written to the configured log sink.
    std::bitset<flag::FLAG_SIZE> flags;  ///< Runtime flags parsed from command-line options.
    std::string credentials_file;  ///< Path to the stored pairing credentials file.

    std::string username;  ///< Username for the local Web UI account.
    std::string password;  ///< Password hash or secret for the local Web UI account.
    std::string salt;  ///< Salt used when hashing the Web UI password.

    std::string config_file;  ///< Path to the active Sunshine configuration file.

    /**
     * @brief Command-line options parsed before configuration loading.
     */
    struct cmd_t {
      std::string name;  ///< Executable name from the command line.
      int argc;  ///< Number of command-line arguments.
      char **argv;  ///< Command-line argument vector.
    } cmd;  ///< Command line used to launch the application.

    std::uint16_t port;  ///< TCP port used by Sunshine services.
    std::string address_family;  ///< Address family requested for listening sockets.
    std::string bind_address;  ///< Local address Sunshine should bind to.

    std::string log_file;  ///< Path to the configured log file.
    bool notify_pre_releases;  ///< Notify users about pre-release updates.

    // List of allowed origins for CSRF protection (e.g., "https://example.com,https://app.example.com")
    // Comma-separated list of additional origins. Default includes localhost variants and web UI port.
    std::vector<std::string> csrf_allowed_origins;  ///< Additional origins allowed by CSRF validation.
  };

  extern video_t video;
  extern stream_t stream;
  extern nvhttp_t nvhttp;
  extern sunshine_t sunshine;

#ifdef SUNSHINE_TESTS
  /**
   * @brief Parse and apply serialized configuration text for unit tests.
   *
   * @param file_content Raw configuration text to parse and apply.
   */
  void apply_config_for_test(std::string_view file_content);
#endif

  /**
   * @brief Parse serialized text into the corresponding runtime representation.
   *
   * @param argc Number of command-line arguments.
   * @param argv Command-line argument vector.
   * @return 0 on success; nonzero when command-line or configuration parsing fails.
   */
  int parse(int argc, char *argv[]);
  /**
   * @brief Parse Sunshine configuration text into key-value entries.
   *
   * @param file_content Raw configuration file contents to parse.
   * @return Parsed configuration key-value entries.
   */
  std::unordered_map<std::string, std::string> parse_config(const std::string_view &file_content);
}  // namespace config
