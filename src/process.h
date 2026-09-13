/**
 * @file src/process.h
 * @brief Application metadata and active Moonlight application state.
 */
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <tuple>
#include <vector>
#include "platform/common.h"
#include "utility.h"

/**
 * @def DEFAULT_APP_IMAGE_PATH
 * @brief Macro for DEFAULT APP IMAGE PATH.
 */
#define DEFAULT_APP_IMAGE_PATH SUNSHINE_ASSETS_DIR "/box.png"

namespace proc {
  /** @brief Metadata exposed in the Moonlight application list. */
  struct ctx_t {
    std::string name;  ///< Display name.
    std::string image_path;  ///< Cover image path.
    std::string id;  ///< Stable application identifier.
  };

  /** @brief Stores application metadata and the active protocol application ID. */
  class proc_t {
  public:
    KITTY_DEFAULT_CONSTR_MOVE_THROW(proc_t)

    /**
     * @brief Construct an application registry.
     * @param apps Application metadata.
     */
    explicit proc_t(std::vector<ctx_t> &&apps):
        _apps(std::move(apps)) {
    }

    /**
     * @brief Select an application for a Moonlight session without launching a local process.
     * @param app_id Configured application ID.
     * @return Zero on selection, or 404 for an unknown application.
     */
    int activate(int app_id);
    /** @brief Return the active application ID, or zero when inactive. */
    int running();
    /** @brief Return configured application metadata. */
    const std::vector<ctx_t> &get_apps() const;
    /** @brief Return mutable configured application metadata. */
    std::vector<ctx_t> &get_apps();
    /**
     * @brief Return a validated cover path.
     * @param app_id Configured application ID.
     * @return Cover path or the default cover.
     */
    std::string get_app_image(int app_id);
    /** @brief Clear active application state and release existing session resources. */
    void terminate();

  private:
    int _app_id {0};  ///< Active protocol application ID.
    std::vector<ctx_t> _apps;  ///< Configured application metadata.
  };

  /**
   * @brief Calculate a stable id based on name and image data
   * @return Tuple of id calculated without index (for use if no collision) and one with.
   *
   * @param app_name App name.
   * @param app_image_path App image path.
   * @param index Zero-based index of the item being addressed.
   */
  std::tuple<std::string, std::string> calculate_app_id(const std::string &app_name, std::string app_image_path, int index);

  bool check_valid_png(const std::filesystem::path &path);
  /**
   * @brief Validate app image path.
   *
   * @param app_image_path Candidate image path from the application configuration.
   * @return Existing PNG path, or the default application image when validation fails.
   */
  std::string validate_app_image_path(std::string app_image_path);
  /**
   * @brief Reload application metadata from the configured file.
   *
   * @param file_name File name.
   */
  void refresh(const std::string &file_name);
  /**
   * @brief Parse serialized text into the corresponding runtime representation.
   *
   * @param file_name File name.
   * @return Parsed value or parse status.
   */
  std::optional<proc::proc_t> parse(const std::string &file_name);

  /**
   * @brief Initialize proc functions
   * @return Unique pointer to `deinit_t` to manage cleanup
   */
  std::unique_ptr<platf::deinit_t> init();

  extern proc_t proc;
}  // namespace proc
