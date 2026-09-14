/**
 * @file src/config.cpp
 * @brief Definitions for the configuration of Sunshine.
 */
// standard includes
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <thread>
#include <unordered_map>
#include <utility>

// lib includes
#include <boost/asio.hpp>
#include <boost/filesystem.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

// local includes
#include "config.h"
#include "entry_handler.h"
#include "file_handler.h"
#include "logging.h"
#include "nvhttp.h"
#include "platform/common.h"
#include "rtsp.h"
#include "utility.h"

#ifdef _WIN32
  #include <shellapi.h>
#endif

namespace fs = std::filesystem;
using namespace std::literals;

constexpr auto CA_DIR = "../../data/sunshine/credentials";  ///< Appdata-relative location of product-owned Sunshine credentials.
const std::string PRIVATE_KEY_FILE = std::string(CA_DIR) + "/cakey.pem";  ///< Relative path to the persisted private key PEM file.
const std::string CERTIFICATE_FILE = std::string(CA_DIR) + "/cacert.pem";  ///< Relative path to the persisted certificate PEM file.
const std::string APPS_JSON_PATH = platf::appdata().string() + "/apps.json";  ///< Default path to the applications JSON file.

namespace config {

  std::string streamhub_socket;
  std::string streamhub_codecs = "h264,hevc";

  video_t video {0};

  stream_t stream {
    10s,  // ping_timeout

    APPS_JSON_PATH,

    20,  // fecPercentage

    ENCRYPTION_MODE_NEVER,  // lan_encryption_mode
    ENCRYPTION_MODE_OPPORTUNISTIC,  // wan_encryption_mode
    0,  // packetsize
  };

  /**
   * @brief Default NVHTTP server configuration values used before file and CLI overrides.
   */
  nvhttp_t nvhttp {
    "lan",  // origin web manager

    PRIVATE_KEY_FILE,
    CERTIFICATE_FILE,

    platf::get_host_name(),  // sunshine_name,
    "../../data/sunshine/state.json"s,  // file_state
    {},  // external_ip
  };

  /**
   * @brief Default input configuration values used before file and CLI overrides.
   */

  /**
   * @brief Default top-level Sunshine configuration values used before file and CLI overrides.
   */
  sunshine_t sunshine {
    "en",  // locale
    2,  // min_log_level
    0,  // flags
    {},  // User file
    {},  // Username
    {},  // Password
    {},  // Password Salt
    platf::appdata().string() + "/sunshine.conf",  // config file
    {},  // cmd args
    47989,  // Base port number
    "ipv4",  // Address family
    {},  // Bind address
    platf::appdata().string() + "/../../logs/sunshine.log",  // log file
    false,  // notify_pre_releases
  };

  /**
   * @brief Return whether a character terminates a configuration line.
   *
   * @param ch Character currently being classified by the parser.
   * @return True when the tested parser condition is met.
   */
  bool endline(char ch) {
    return ch == '\r' || ch == '\n';
  }

  /**
   * @brief Return whether a character is horizontal parser whitespace.
   *
   * @param ch Character currently being classified by the parser.
   * @return True when the tested parser condition is met.
   */
  bool space_tab(char ch) {
    return ch == ' ' || ch == '\t';
  }

  /**
   * @brief Return whether a character should be treated as parser whitespace.
   *
   * @param ch Character currently being classified by the parser.
   * @return True when the tested parser condition is met.
   */
  bool whitespace(char ch) {
    return space_tab(ch) || endline(ch);
  }

  /**
   * @brief Copy a configuration text range while stripping inline comments.
   *
   * @param begin Iterator or pointer marking the start of the input range.
   * @param end Iterator or pointer marking the end of the input range.
   * @return Value converted to string.
   */
  std::string to_string(const char *begin, const char *end) {
    std::string result;

    KITTY_WHILE_LOOP(auto pos = begin, pos != end, {
      auto comment = std::find(pos, end, '#');
      auto endl = std::find_if(comment, end, endline);

      result.append(pos, comment);

      pos = endl;
    })

    return result;
  }

  /**
   * @brief Advance over a bracketed list while honoring nested brackets.
   *
   * @param skipper Function used to skip characters while parsing.
   * @param end Iterator or pointer marking the end of the input range.
   * @return Iterator positioned after the matching closing bracket or at the end.
   */
  template<class It>
  It skip_list(It skipper, It end) {
    int stack = 1;
    while (skipper != end && stack) {
      if (*skipper == '[') {
        ++stack;
      }
      if (*skipper == ']') {
        --stack;
      }

      ++skipper;
    }

    return skipper;
  }

  std::pair<
    std::string_view::const_iterator,
    std::optional<std::pair<std::string, std::string>>>
    /**
     * @brief Parse one `name = value` configuration entry.
     *
     * @param begin Iterator or pointer marking the start of the input range.
     * @param end Iterator or pointer marking the end of the input range.
     * @return Iterator for the next line and the parsed key-value pair when one was found.
     */
    parse_option(std::string_view::const_iterator begin, std::string_view::const_iterator end) {
    begin = std::find_if_not(begin, end, whitespace);
    auto endl = std::find_if(begin, end, endline);
    auto endc = std::find(begin, endl, '#');
    endc = std::find_if(std::make_reverse_iterator(endc), std::make_reverse_iterator(begin), std::not_fn(whitespace)).base();

    auto eq = std::find(begin, endc, '=');
    if (eq == endc || eq == begin) {
      return std::make_pair(endl, std::nullopt);
    }

    auto end_name = std::find_if_not(std::make_reverse_iterator(eq), std::make_reverse_iterator(begin), space_tab).base();
    auto begin_val = std::find_if_not(eq + 1, endc, space_tab);

    if (begin_val == endl) {
      return std::make_pair(endl, std::nullopt);
    }

    // Lists might contain newlines
    if (*begin_val == '[') {
      endl = skip_list(begin_val + 1, end);

      // Check if we reached the end of the file without finding a closing bracket
      // We know we have a valid closing bracket if:
      // 1. We didn't reach the end, or
      // 2. We reached the end but the last character was the matching closing bracket
      if (endl == end && end == begin_val + 1) {
        BOOST_LOG(warning) << "config: Missing ']' in config option: " << to_string(begin, end_name);
        return std::make_pair(endl, std::nullopt);
      }
    }

    return std::make_pair(
      endl,
      std::make_pair(to_string(begin, end_name), to_string(begin_val, endl))
    );
  }

  /**
   * @brief Parse Sunshine configuration text into key-value entries.
   */
  std::unordered_map<std::string, std::string> parse_config(const std::string_view &file_content) {
    std::unordered_map<std::string, std::string> vars;

    auto pos = std::begin(file_content);
    auto end = std::end(file_content);

    while (pos < end) {
      // auto newline = std::find_if(pos, end, [](auto ch) { return ch == '\n' || ch == '\r'; });
      TUPLE_2D(endl, var, parse_option(pos, end));

      pos = endl;
      if (pos != end) {
        pos += (*pos == '\r') ? 2 : 1;
      }

      if (!var) {
        continue;
      }

      vars.emplace(std::move(*var));
    }

    return vars;
  }

  /**
   * @brief Consume a string setting from the parsed configuration map.
   *
   * @param vars Parsed configuration entries; consumed keys are erased.
   * @param name Configuration key to consume.
   * @param input Destination field updated when the setting exists and parses successfully.
   */
  void string_f(std::unordered_map<std::string, std::string> &vars, const std::string &name, std::string &input) {
    auto it = vars.find(name);
    if (it == std::end(vars)) {
      return;
    }

    input = std::move(it->second);

    vars.erase(it);
  }

  /**
   * @brief Consume a setting and convert it with a caller-provided parser.
   *
   * @param vars Parsed configuration entries; consumed keys are erased.
   * @param name Configuration key to consume.
   * @param input Destination field updated when the setting exists and parses successfully.
   * @param f Converter applied to the raw configuration string.
   */
  template<typename T, typename F>
  void generic_f(std::unordered_map<std::string, std::string> &vars, const std::string &name, T &input, F &&f) {
    std::string tmp;
    string_f(vars, name, tmp);
    if (!tmp.empty()) {
      input = f(tmp);
    }
  }

  /**
   * @brief Consume a string setting only when it matches an allowed value.
   *
   * @param vars Parsed configuration entries; consumed keys are erased.
   * @param name Configuration key to consume.
   * @param input Destination field updated when the setting exists and parses successfully.
   * @param allowed_vals Accepted string values for this setting.
   */
  void string_restricted_f(std::unordered_map<std::string, std::string> &vars, const std::string &name, std::string &input, const std::vector<std::string_view> &allowed_vals) {
    std::string temp;
    string_f(vars, name, temp);

    for (auto &allowed_val : allowed_vals) {
      if (temp == allowed_val) {
        input = std::move(temp);
        return;
      }
    }
  }

  /**
   * @brief Parse a comma-separated string setting into a list.
   *
   * @param vars Configuration key-value map.
   * @param name Setting name.
   * @param output Parsed string list.
   */
  void string_list_f(std::unordered_map<std::string, std::string> &vars, const std::string &name, std::vector<std::string> &output) {  // NOSONAR(cpp:S6045): transparent hasher not available for unordered_map in this codebase
    std::string temp;
    string_f(vars, name, temp);

    if (temp.empty()) {
      return;
    }

    output.clear();
    std::stringstream ss(temp);
    std::string item;
    while (std::getline(ss, item, ',')) {
      // Trim whitespace
      item.erase(0, item.find_first_not_of(" \t\r\n"));
      item.erase(item.find_last_not_of(" \t\r\n") + 1);
      if (!item.empty()) {
        output.push_back(item);
      }
    }
  }

  /**
   * @brief Reject writable paths outside product var, including symlink escapes.
   * @param input Absolute or appdata-relative output path.
   * @return Canonical output path below the product writable directory.
   */
  fs::path product_path(const fs::path &input) {
    const char *root = std::getenv("STREAMHUB_ROOT");
    if (!root || !*root || !fs::path(root).is_absolute() || fs::path(root) == "/") {
      throw fs::filesystem_error("STREAMHUB_ROOT is required", input, std::make_error_code(std::errc::permission_denied));
    }
    const auto base = fs::weakly_canonical(fs::path(root));
    const auto var = base / "var";
    const auto path = fs::weakly_canonical(input.is_absolute() ? input : platf::appdata() / input);
    const auto relative = path.lexically_relative(var);
    if (relative.empty() || relative == "." || *relative.begin() == "..") {
      throw fs::filesystem_error("output must remain below STREAMHUB_ROOT/var", path, std::make_error_code(std::errc::permission_denied));
    }
    return path;
  }

  /**
   * @brief Consume a path setting and normalize it under the app data directory when relative.
   *
   * @param vars Parsed configuration entries; consumed keys are erased.
   * @param name Configuration key to consume.
   * @param input Destination field updated when the setting exists and parses successfully.
   */
  void path_f(std::unordered_map<std::string, std::string> &vars, const std::string &name, fs::path &input) {
    // appdata needs to be retrieved once only
    static auto appdata = platf::appdata();

    std::string temp;
    string_f(vars, name, temp);

    if (!temp.empty()) {
      input = temp;
    }

    if (input.is_relative()) {
      input = appdata / input;
    }

    input = product_path(input);

    auto dir = input;
    dir.remove_filename();

    // Ensure the directories exists
    if (!fs::exists(dir)) {
      fs::create_directories(dir);
    }
  }

  /**
   * @brief Consume a path setting and normalize it under the app data directory when relative.
   *
   * @param vars Parsed configuration entries; consumed keys are erased.
   * @param name Configuration key to consume.
   * @param input Destination field updated when the setting exists and parses successfully.
   */
  void path_f(std::unordered_map<std::string, std::string> &vars, const std::string &name, std::string &input) {
    fs::path temp = input;

    path_f(vars, name, temp);

    input = temp.string();
  }

  /**
   * @brief Parse a decimal or hexadecimal integer configuration value.
   *
   * @param value Raw configuration value, optionally surrounded by quotes.
   * @return Parsed integer value.
   */
  int parse_config_integer(std::string_view value) {
    if (value.size() >= 2 && value.front() == '"') {
      value = value.substr(1, value.size() - 2);
    }

    if (value.starts_with("0x"sv)) {
      return util::from_hex<int>(value.substr(2));
    }
    return static_cast<int>(util::from_view(value));
  }

  /**
   * @brief Consume an integer setting from decimal or hexadecimal configuration text.
   *
   * @param vars Parsed configuration entries; consumed keys are erased.
   * @param name Configuration key to consume.
   * @param input Destination field updated when the setting exists and parses successfully.
   */
  void int_f(std::unordered_map<std::string, std::string> &vars, const std::string &name, int &input) {
    auto it = vars.find(name);

    if (it == std::end(vars)) {
      return;
    }

    input = parse_config_integer(it->second);
    vars.erase(it);
  }

  /**
   * @brief Consume an integer setting from decimal or hexadecimal configuration text.
   *
   * @param vars Parsed configuration entries; consumed keys are erased.
   * @param name Configuration key to consume.
   * @param input Destination field updated when the setting exists and parses successfully.
   */
  void int_f(std::unordered_map<std::string, std::string> &vars, const std::string &name, std::optional<int> &input) {
    auto it = vars.find(name);

    if (it == std::end(vars)) {
      return;
    }

    input = parse_config_integer(it->second);
    vars.erase(it);
  }

  /**
   * @brief Consume an integer setting from decimal or hexadecimal configuration text.
   *
   * @param vars Parsed configuration entries; consumed keys are erased.
   * @param name Configuration key to consume.
   * @param input Destination field updated when the setting exists and parses successfully.
   * @param f Converter applied to the raw configuration string.
   */
  template<class F>
  void int_f(std::unordered_map<std::string, std::string> &vars, const std::string &name, int &input, F &&f) {
    std::string tmp;
    string_f(vars, name, tmp);
    if (!tmp.empty()) {
      input = f(tmp);
    }
  }

  /**
   * @brief Consume an integer setting from decimal or hexadecimal configuration text.
   *
   * @param vars Parsed configuration entries; consumed keys are erased.
   * @param name Configuration key to consume.
   * @param input Destination field updated when the setting exists and parses successfully.
   * @param f Converter applied to the raw configuration string.
   */
  template<class F>
  void int_f(std::unordered_map<std::string, std::string> &vars, const std::string &name, std::optional<int> &input, F &&f) {
    std::string tmp;
    string_f(vars, name, tmp);
    if (!tmp.empty()) {
      input = f(tmp);
    }
  }

  /**
   * @brief Consume an integer setting only when it falls inside an inclusive range.
   *
   * @param vars Parsed configuration entries; consumed keys are erased.
   * @param name Configuration key to consume.
   * @param input Destination field updated when the setting exists and parses successfully.
   * @param range Inclusive range accepted for the parsed value.
   */
  void int_between_f(std::unordered_map<std::string, std::string> &vars, const std::string &name, int &input, const std::pair<int, int> &range) {
    int temp = input;

    int_f(vars, name, temp);

    TUPLE_2D_REF(lower, upper, range);
    if (temp >= lower && temp <= upper) {
      input = temp;
    }
  }

  /**
   * @brief Convert common textual boolean forms to a boolean value.
   *
   * @param boolean Configuration string to classify as enabled or disabled.
   * @return True when the tested parser condition is met.
   */
  bool to_bool(std::string &boolean) {
    std::for_each(std::begin(boolean), std::end(boolean), [](char ch) {
      return (char) std::tolower(ch);
    });

    return boolean == "true"sv ||
           boolean == "yes"sv ||
           boolean == "enable"sv ||
           boolean == "enabled"sv ||
           boolean == "on"sv ||
           (std::find(std::begin(boolean), std::end(boolean), '1') != std::end(boolean));
  }

  /**
   * @brief Consume a boolean setting from the parsed configuration map.
   *
   * @param vars Parsed configuration entries; consumed keys are erased.
   * @param name Configuration key to consume.
   * @param input Destination field updated when the setting exists and parses successfully.
   */
  void bool_f(std::unordered_map<std::string, std::string> &vars, const std::string &name, bool &input) {
    std::string tmp;
    string_f(vars, name, tmp);

    if (tmp.empty()) {
      return;
    }

    input = to_bool(tmp);
  }

  /**
   * @brief Consume a floating-point setting from the parsed configuration map.
   *
   * @param vars Parsed configuration entries; consumed keys are erased.
   * @param name Configuration key to consume.
   * @param input Destination field updated when the setting exists and parses successfully.
   */
  void double_f(std::unordered_map<std::string, std::string> &vars, const std::string &name, double &input) {
    std::string tmp;
    string_f(vars, name, tmp);

    if (tmp.empty()) {
      return;
    }

    char *c_str_p;
    auto val = std::strtod(tmp.c_str(), &c_str_p);

    if (c_str_p == tmp.c_str()) {
      return;
    }

    input = val;
  }

  /**
   * @brief Consume a floating-point setting only when it falls inside an inclusive range.
   *
   * @param vars Parsed configuration entries; consumed keys are erased.
   * @param name Configuration key to consume.
   * @param input Destination field updated when the setting exists and parses successfully.
   * @param range Inclusive range accepted for the parsed value.
   */
  void double_between_f(std::unordered_map<std::string, std::string> &vars, const std::string &name, double &input, const std::pair<double, double> &range) {
    double temp = input;

    double_f(vars, name, temp);

    TUPLE_2D_REF(lower, upper, range);
    if (temp >= lower && temp <= upper) {
      input = temp;
    }
  }

  /**
   * @brief Consume a comma-separated or bracketed string list setting.
   *
   * @param vars Parsed configuration entries; consumed keys are erased.
   * @param name Configuration key to consume.
   * @param input Destination field updated when the setting exists and parses successfully.
   */

  /**
   * @brief Consume an integer list setting from decimal or hexadecimal configuration text.
   *
   * @param vars Parsed configuration entries; consumed keys are erased.
   * @param name Configuration key to consume.
   * @param input Destination field updated when the setting exists and parses successfully.
   */

  /**
   * @brief Consume an integer-pair list into a mapping table.
   *
   * @param vars Parsed configuration entries; consumed keys are erased.
   * @param name Configuration key to consume.
   * @param input Destination field updated when the setting exists and parses successfully.
   */

  /**
   * @brief Apply single-character command-line flags to the global Sunshine flags bitset.
   *
   * @param line Configuration line being parsed.
   * @return 0 when all flags are recognized; -1 when at least one flag is unknown.
   */
  int apply_flags(const char *line) {
    int ret = 0;
    while (*line != '\0') {
      switch (*line) {
        case '0':
          config::sunshine.flags[config::flag::PIN_STDIN].flip();
          break;
        case '1':
          config::sunshine.flags[config::flag::FRESH_STATE].flip();
          break;
        case 'p':
          config::sunshine.flags[config::flag::UPNP].flip();
          break;
        default:
          BOOST_LOG(warning) << "config: Unrecognized flag: ["sv << *line << ']' << std::endl;
          ret = -1;
      }

      ++line;
    }

    return ret;
  }

  /**
   * @brief Get supported gamepad options.
   *
   * @return Platform-supported gamepad backend names accepted by configuration.
   */

  /**
   * @brief Log parsed configuration entries and optionally record them as modified.
   */
  void log_config_settings(const std::unordered_map<std::string, std::string> &vars, bool save) {
    for (auto &[name, val] : vars) {
      bool is_redacted = std::ranges::find(config::redacted_config, name) != config::redacted_config.end();

      BOOST_LOG(info) << "config: '"sv << name << "' = "sv << (is_redacted ? "[redacted]" : val);

      if (save) {
        modified_config_settings[name] = val;
      }
    }
  }

  /**
   * @brief Apply parsed configuration entries to the global runtime configuration.
   *
   * @param vars Parsed configuration entries; consumed keys are erased.
   */
  void apply_config(std::unordered_map<std::string, std::string> &&vars) {
    log_config_settings(vars, true);

    int_f(vars, "max_bitrate", video.max_bitrate);

    path_f(vars, "pkey", nvhttp.pkey);
    path_f(vars, "cert", nvhttp.cert);
    string_f(vars, "sunshine_name", nvhttp.sunshine_name);
    string_f(vars, "streamhub_socket", streamhub_socket);
    string_restricted_f(vars, "streamhub_codecs", streamhub_codecs, {"h264"sv, "hevc"sv, "h264,hevc"sv});
    path_f(vars, "log_path", config::sunshine.log_file);
    path_f(vars, "file_state", nvhttp.file_state);

    // Must be run after "file_state"
    config::sunshine.credentials_file = config::nvhttp.file_state;
    path_f(vars, "credentials_file", config::sunshine.credentials_file);

    string_f(vars, "external_ip", nvhttp.external_ip);

    string_restricted_f(vars, "origin_web_ui_allowed", nvhttp.origin_web_ui_allowed, {"pc"sv, "lan"sv, "wan"sv});

    // Parse CSRF allowed origins - always include defaults, then append user-configured origins
    std::vector<std::string> user_csrf_origins;
    string_list_f(vars, "csrf_allowed_origins", user_csrf_origins);

    // Start with default localhost variants
    sunshine.csrf_allowed_origins = {
      "https://localhost",
      "https://127.0.0.1",
      "https://[::1]"
    };

    // Validate and append user-configured origins
    bool csrf_invalid_config = false;
    for (const auto &origin : user_csrf_origins) {
      if (origin.size() > 8 && origin.starts_with("https://")) {
        sunshine.csrf_allowed_origins.push_back(origin);
      } else {
        csrf_invalid_config = true;
        BOOST_LOG(warning) << "Invalid 'csrf_allowed_origins' entry rejected: "sv << origin;
      }
    }
    if (csrf_invalid_config) {
      BOOST_LOG(warning) << "Please refer to: https://docs.lizardbyte.dev/projects/sunshine/latest/md_docs_2configuration.html#csrf_allowed_origins"sv;
    }

    int to = -1;
    int_between_f(vars, "ping_timeout", to, {-1, std::numeric_limits<int>::max()});
    if (to != -1) {
      stream.ping_timeout = std::chrono::milliseconds(to);
    }

    int_between_f(vars, "lan_encryption_mode", stream.lan_encryption_mode, {0, 2});
    int_between_f(vars, "wan_encryption_mode", stream.wan_encryption_mode, {0, 2});
    int_between_f(vars, "packetsize", stream.packetsize, {0, PACKETSIZE_MAX});

    path_f(vars, "file_apps", stream.file_apps);
#ifndef __ANDROID__
    // TODO: Android can possibly support this
    if (!fs::exists(stream.file_apps.c_str())) {
      fs::copy_file(SUNSHINE_ASSETS_DIR "/apps.json", stream.file_apps);
      fs::permissions(
        stream.file_apps,
        fs::perms::owner_read | fs::perms::owner_write,
        fs::perm_options::add
      );
    }
#endif

    int_between_f(vars, "fec_percentage", stream.fec_percentage, {1, 255});

    bool_f(vars, "notify_pre_releases", sunshine.notify_pre_releases);

    int port = sunshine.port;
    int_between_f(vars, "port"s, port, {1024 + nvhttp::PORT_HTTPS, 65535 - rtsp_stream::RTSP_SETUP_PORT});
    sunshine.port = (std::uint16_t) port;

    // Now that we have the port, add web UI port-specific origins to CSRF allowed list
    // Web UI runs on port + 1 (PORT_HTTPS offset is 1 for confighttp)
    const unsigned short web_ui_port = sunshine.port + 1;
    sunshine.csrf_allowed_origins.push_back(std::format("https://localhost:{}", web_ui_port));
    sunshine.csrf_allowed_origins.push_back(std::format("https://127.0.0.1:{}", web_ui_port));
    sunshine.csrf_allowed_origins.push_back(std::format("https://[::1]:{}", web_ui_port));

    string_restricted_f(vars, "address_family", sunshine.address_family, {"ipv4"sv, "both"sv});
    string_f(vars, "bind_address", sunshine.bind_address);

    bool upnp = false;
    bool_f(vars, "upnp"s, upnp);

    if (upnp) {
      config::sunshine.flags[config::flag::UPNP].flip();
    }

    string_restricted_f(vars, "locale", config::sunshine.locale, {
                                                                   "bg"sv,  // Bulgarian
                                                                   "cs"sv,  // Czech
                                                                   "de"sv,  // German
                                                                   "en"sv,  // English
                                                                   "en_GB"sv,  // English (UK)
                                                                   "en_US"sv,  // English (US)
                                                                   "es"sv,  // Spanish
                                                                   "fr"sv,  // French
                                                                   "hu"sv,  // Hungarian
                                                                   "it"sv,  // Italian
                                                                   "ja"sv,  // Japanese
                                                                   "ko"sv,  // Korean
                                                                   "pl"sv,  // Polish
                                                                   "pt"sv,  // Portuguese
                                                                   "pt_BR"sv,  // Portuguese (Brazilian)
                                                                   "ru"sv,  // Russian
                                                                   "sv"sv,  // Swedish
                                                                   "tr"sv,  // Turkish
                                                                   "uk"sv,  // Ukrainian
                                                                   "vi"sv,  // Vietnamese
                                                                   "zh"sv,  // Chinese
                                                                   "zh_TW"sv,  // Chinese (Traditional)
                                                                 });

    std::string log_level_string;
    string_f(vars, "min_log_level", log_level_string);

    if (!log_level_string.empty()) {
      if (log_level_string == "verbose"sv) {
        sunshine.min_log_level = 0;
      } else if (log_level_string == "debug"sv) {
        sunshine.min_log_level = 1;
      } else if (log_level_string == "info"sv) {
        sunshine.min_log_level = 2;
      } else if (log_level_string == "warning"sv) {
        sunshine.min_log_level = 3;
      } else if (log_level_string == "error"sv) {
        sunshine.min_log_level = 4;
      } else if (log_level_string == "fatal"sv) {
        sunshine.min_log_level = 5;
      } else if (log_level_string == "none"sv) {
        sunshine.min_log_level = 6;
      } else {
        // accept digit directly
        auto val = log_level_string[0];
        if (val >= '0' && val < '7') {
          sunshine.min_log_level = val - '0';
        }
      }
    }

    auto it = vars.find("flags"s);
    if (it != std::end(vars)) {
      apply_flags(it->second.c_str());

      vars.erase(it);
    }

    if (sunshine.min_log_level <= 3) {
      for (auto &[var, _] : vars) {
        std::cout << "Warning: Unrecognized configurable option ["sv << var << ']' << std::endl;
      }
    }
  }

#ifdef SUNSHINE_TESTS
  /**
   * @brief Parse and apply serialized configuration text for unit tests.
   *
   * @param file_content Raw configuration text to parse and apply.
   */
  void apply_config_for_test(const std::string_view file_content) {
    apply_config(parse_config(file_content));
  }
#endif

  /**
   * @brief Parse serialized text into the corresponding runtime representation.
   */
  int parse(int argc, char *argv[]) {
    std::unordered_map<std::string, std::string> cmd_vars;
#ifdef _WIN32
    bool shortcut_launch = false;
    bool service_admin_launch = false;
#endif

    for (auto x = 1; x < argc; ++x) {
      auto line = argv[x];

      if (line == "--help"sv) {
        logging::print_help(*argv);
        return 1;
      }
#ifdef _WIN32
      else if (line == "--shortcut"sv) {
        shortcut_launch = true;
      } else if (line == "--shortcut-admin"sv) {
        service_admin_launch = true;
      }
#endif
      else if (*line == '-') {
        if (*(line + 1) == '-') {
          sunshine.cmd.name = line + 2;
          sunshine.cmd.argc = argc - x - 1;
          sunshine.cmd.argv = argv + x + 1;

          break;
        }
        if (apply_flags(line + 1)) {
          logging::print_help(*argv);
          return -1;
        }
      } else {
        auto line_end = line + strlen(line);

        auto pos = std::find(line, line_end, '=');
        if (pos == line_end) {
          sunshine.config_file = line;
        } else {
          TUPLE_EL(var, 1, parse_option(line, line_end));
          if (!var) {
            logging::print_help(*argv);
            return -1;
          }

          TUPLE_EL_REF(name, 0, *var);

          auto it = cmd_vars.find(name);
          if (it != std::end(cmd_vars)) {
            cmd_vars.erase(it);
          }

          cmd_vars.emplace(std::move(*var));
        }
      }
    }

    bool config_loaded = false;
    try {
      sunshine.config_file = product_path(sunshine.config_file).string();
      product_path(platf::appdata());
      // Create appdata folder if it does not exist
      file_handler::make_directory(platf::appdata().string());

      // Create empty config file if it does not exist
      if (!fs::exists(sunshine.config_file)) {
        std::ofstream {sunshine.config_file};
      }

      // Read config file
      auto vars = parse_config(file_handler::read_file(sunshine.config_file.c_str()));

      for (auto &[name, value] : cmd_vars) {
        vars.insert_or_assign(std::move(name), std::move(value));
      }

      // Apply the config. Note: This will try to create any paths
      // referenced in the config, so we may receive exceptions if
      // the path is incorrect or inaccessible.
      apply_config(std::move(vars));
      config_loaded = true;
    } catch (const std::filesystem::filesystem_error &err) {
      BOOST_LOG(fatal) << "Failed to apply config: "sv << err.what();
    } catch (const boost::filesystem::filesystem_error &err) {
      BOOST_LOG(fatal) << "Failed to apply config: "sv << err.what();
    }

#ifdef _WIN32
    // UCRT64 raises an access denied exception if launching from the shortcut
    // as non-admin and the config folder is not yet present; we can defer
    // so that service instance will do the work instead.

    if (!config_loaded && !shortcut_launch) {
      BOOST_LOG(fatal) << "To relaunch Sunshine successfully, use the shortcut in the Start Menu. Do not run Sunshine.exe manually."sv;
      std::this_thread::sleep_for(10s);
#else
    if (!config_loaded) {
#endif
      return -1;
    }

#ifdef _WIN32
    // We have to wait until the config is loaded to handle these launches,
    // because we need to have the correct base port loaded in our config.
    // Exception: UCRT64 shortcut_launch instances may have no config loaded due to
    // insufficient permissions to create folder; port defaults will be acceptable.
    if (service_admin_launch) {
      // This is a relaunch as admin to start the service
      service_ctrl::start_service();

      // Always return 1 to ensure Sunshine doesn't start normally
      return 1;
    }
    if (shortcut_launch) {
      if (!service_ctrl::is_service_running()) {
        // If the service isn't running, relaunch ourselves as admin to start it
        WCHAR executable[MAX_PATH];
        GetModuleFileNameW(nullptr, executable, ARRAYSIZE(executable));

        SHELLEXECUTEINFOW shell_exec_info {};
        shell_exec_info.cbSize = sizeof(shell_exec_info);
        shell_exec_info.fMask = SEE_MASK_NOASYNC | SEE_MASK_NO_CONSOLE | SEE_MASK_NOCLOSEPROCESS;
        shell_exec_info.lpVerb = L"runas";
        shell_exec_info.lpFile = executable;
        shell_exec_info.lpParameters = L"--shortcut-admin";
        shell_exec_info.nShow = SW_NORMAL;
        if (!ShellExecuteExW(&shell_exec_info)) {
          auto winerr = GetLastError();
          BOOST_LOG(error) << "Failed executing shell command: " << winerr << std::endl;
          return 1;
        }

        // Wait for the elevated process to finish starting the service
        WaitForSingleObject(shell_exec_info.hProcess, INFINITE);
        CloseHandle(shell_exec_info.hProcess);

        // Wait for the UI to be ready for connections
        service_ctrl::wait_for_ui_ready();
      }

      // Always return 1 to ensure Sunshine doesn't start normally
      return 1;
    }
#endif

    return 0;
  }
}  // namespace config
