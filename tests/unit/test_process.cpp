/**
 * @file tests/unit/test_process.cpp
 * @brief Test src/process.* functions.
 */
// test includes
#include "../tests_common.h"

// standard includes
#include <filesystem>
#include <fstream>

// local includes
#include <src/process.h>

namespace fs = std::filesystem;

class ProcessPNGTest: public BaseTest {
protected:
  void SetUp() override {
    BaseTest::SetUp();
    // Create test directory
    test_dir = fs::temp_directory_path() / "sunshine_process_png_test";  // NOSONAR(cpp:S5443): safe for tests
    fs::create_directories(test_dir);
  }

  void TearDown() override {
    // Clean up test directory
    if (fs::exists(test_dir)) {
      fs::remove_all(test_dir);
    }
    BaseTest::TearDown();
  }

  // Helper function to create a file with specific content
  void createTestFile(const fs::path &path, const std::vector<unsigned char> &content) const {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char *>(content.data()), content.size());
    file.close();
  }

  fs::path test_dir;
};

// Tests for check_valid_png function
TEST_F(ProcessPNGTest, CheckValidPNG_ValidSignature) {
  // Valid PNG signature
  const std::vector<unsigned char> valid_png_data = {
    0x89,
    0x50,
    0x4E,
    0x47,
    0x0D,
    0x0A,
    0x1A,
    0x0A,  // PNG signature
    // Add some dummy data to make it more realistic
    0x00,
    0x00,
    0x00,
    0x0D,
    0x49,
    0x48,
    0x44,
    0x52
  };

  const fs::path test_file = test_dir / "valid.png";
  createTestFile(test_file, valid_png_data);

  EXPECT_TRUE(proc::check_valid_png(test_file));
}

TEST_F(ProcessPNGTest, CheckValidPNG_WrongSignature) {
  // Invalid PNG signature (wrong magic bytes)
  const std::vector<unsigned char> invalid_png_data = {
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00
  };

  const fs::path test_file = test_dir / "invalid.png";
  createTestFile(test_file, invalid_png_data);

  EXPECT_FALSE(proc::check_valid_png(test_file));
}

TEST_F(ProcessPNGTest, CheckValidPNG_TooShort) {
  // File too short (less than 8 bytes)
  const std::vector<unsigned char> short_data = {
    0x89,
    0x50,
    0x4E,
    0x47
  };

  const fs::path test_file = test_dir / "short.png";
  createTestFile(test_file, short_data);

  EXPECT_FALSE(proc::check_valid_png(test_file));
}

TEST_F(ProcessPNGTest, CheckValidPNG_EmptyFile) {
  // Empty file
  const std::vector<unsigned char> empty_data = {};

  const fs::path test_file = test_dir / "empty.png";
  createTestFile(test_file, empty_data);

  EXPECT_FALSE(proc::check_valid_png(test_file));
}

TEST_F(ProcessPNGTest, CheckValidPNG_NonExistentFile) {
  // File doesn't exist
  const fs::path test_file = test_dir / "nonexistent.png";

  EXPECT_FALSE(proc::check_valid_png(test_file));
}

TEST_F(ProcessPNGTest, CheckValidPNG_RealFile) {
  // Test with the actual sunshine.png from the project root

  // Only run this test if the file exists
  if (const fs::path sunshine_png = fs::path(SUNSHINE_SOURCE_DIR) / "sunshine.png"; fs::exists(sunshine_png)) {
    EXPECT_TRUE(proc::check_valid_png(sunshine_png));
  } else {
    GTEST_SKIP() << "sunshine.png not found in project root";
  }
}

TEST_F(ProcessPNGTest, CheckValidPNG_JPEGFile) {
  // JPEG signature (not PNG)
  const std::vector<unsigned char> jpeg_data = {
    0xFF,
    0xD8,
    0xFF,
    0xE0,
    0x00,
    0x10,
    0x4A,
    0x46
  };

  const fs::path test_file = test_dir / "fake.png";
  createTestFile(test_file, jpeg_data);

  EXPECT_FALSE(proc::check_valid_png(test_file));
}

TEST_F(ProcessPNGTest, CheckValidPNG_PartialSignature) {
  // Partial PNG signature (first 4 bytes correct, rest wrong)
  const std::vector<unsigned char> partial_png_data = {
    0x89,
    0x50,
    0x4E,
    0x47,
    0x00,
    0x00,
    0x00,
    0x00
  };

  const fs::path test_file = test_dir / "partial.png";
  createTestFile(test_file, partial_png_data);

  EXPECT_FALSE(proc::check_valid_png(test_file));
}

// Tests for validate_app_image_path function
TEST_F(ProcessPNGTest, ValidateAppImagePath_EmptyPath) {
  // Empty path should return default
  const std::string result = proc::validate_app_image_path("");
  EXPECT_EQ(result, DEFAULT_APP_IMAGE_PATH);
}

TEST_F(ProcessPNGTest, ValidateAppImagePath_NonPNGExtension) {
  // Non-PNG extension should return default
  const std::string result = proc::validate_app_image_path("image.jpg");
  EXPECT_EQ(result, DEFAULT_APP_IMAGE_PATH);
}

TEST_F(ProcessPNGTest, ValidateAppImagePath_CaseInsensitiveExtension) {
  // Test that .PNG (uppercase) is recognized
  // Create a valid PNG file
  const std::vector<unsigned char> valid_png_data = {
    0x89,
    0x50,
    0x4E,
    0x47,
    0x0D,
    0x0A,
    0x1A,
    0x0A,
    0x00,
    0x00,
    0x00,
    0x0D,
    0x49,
    0x48,
    0x44,
    0x52
  };

  const fs::path test_file = test_dir / "test.PNG";
  createTestFile(test_file, valid_png_data);

  const std::string result = proc::validate_app_image_path(test_file.string());
  // Should accept uppercase .PNG extension
  EXPECT_NE(result, DEFAULT_APP_IMAGE_PATH);
}

TEST_F(ProcessPNGTest, ValidateAppImagePath_NonExistentFile) {
  // Non-existent PNG file should return default
  const std::string result = proc::validate_app_image_path("/nonexistent/path/image.png");
  EXPECT_EQ(result, DEFAULT_APP_IMAGE_PATH);
}

TEST_F(ProcessPNGTest, ValidateAppImagePath_InvalidPNGSignature) {
  // File with .png extension but invalid signature should return default
  const std::vector<unsigned char> invalid_data = {
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00
  };

  const fs::path test_file = test_dir / "invalid.png";
  createTestFile(test_file, invalid_data);

  const std::string result = proc::validate_app_image_path(test_file.string());
  EXPECT_EQ(result, DEFAULT_APP_IMAGE_PATH);
}

TEST_F(ProcessPNGTest, ValidateAppImagePath_ValidPNG) {
  // Valid PNG file should return the path
  const std::vector<unsigned char> valid_png_data = {
    0x89,
    0x50,
    0x4E,
    0x47,
    0x0D,
    0x0A,
    0x1A,
    0x0A,
    0x00,
    0x00,
    0x00,
    0x0D,
    0x49,
    0x48,
    0x44,
    0x52
  };

  const fs::path test_file = test_dir / "valid.png";
  createTestFile(test_file, valid_png_data);

  const std::string result = proc::validate_app_image_path(test_file.string());
  EXPECT_EQ(result, test_file.string());
}

TEST_F(ProcessPNGTest, ValidateAppImagePath_OldSteamDefault) {
  // Test the special case for old steam image path
  const std::string result = proc::validate_app_image_path("./assets/steam.png");
  EXPECT_EQ(result, SUNSHINE_ASSETS_DIR "/steam.png");
}

/** @brief Metadata parsing and session selection must not execute legacy commands. */
TEST_F(ProcessPNGTest, LegacyCommandsAreIgnoredDuringSessionLifecycle) {
  const auto marker = test_dir / "must-not-exist";
  const auto config_file = test_dir / "apps.json";
  std::ofstream config(config_file);
  config << R"APP({"env":{"NAME":"expanded"},"apps":[{"name":"$(NAME)","cmd":"touch )APP"
         << marker.string()
         << R"(","prep-cmd":[{"do":"touch )" << marker.string()
         << R"(","undo":"touch )" << marker.string()
         << R"("}],"detached":["touch )" << marker.string()
         << R"("],"elevated":true,"exit-timeout":600}]})";
  config.close();

  auto registry = proc::parse(config_file.string());
  ASSERT_TRUE(registry);
  ASSERT_EQ(registry->get_apps().size(), 1);
  EXPECT_EQ(registry->get_apps()[0].name, "$(NAME)");
  EXPECT_EQ(registry->running(), 0);
  const int id = std::stoi(registry->get_apps()[0].id);
  EXPECT_EQ(registry->activate(id), 0);
  EXPECT_EQ(registry->running(), id);
  EXPECT_FALSE(fs::exists(marker));
  registry->terminate();
  EXPECT_EQ(registry->running(), 0);
  EXPECT_FALSE(fs::exists(marker));
}

/** @brief An unknown application must not replace the selected application. */
TEST_F(ProcessPNGTest, UnknownApplicationDoesNotChangeSelection) {
  proc::proc_t registry(std::vector<proc::ctx_t> {{"One", "", "1"}, {"Two", "", "2"}});
  EXPECT_EQ(registry.activate(99), 404);
  EXPECT_EQ(registry.running(), 0);
  EXPECT_EQ(registry.activate(1), 0);
  EXPECT_EQ(registry.activate(99), 404);
  EXPECT_EQ(registry.running(), 1);
  EXPECT_EQ(registry.activate(2), 0);
  EXPECT_EQ(registry.running(), 2);
  registry.terminate();
  registry.terminate();
  EXPECT_EQ(registry.running(), 0);
}

/** @brief Metadata-only files retain the existing name-based stable IDs. */
TEST_F(ProcessPNGTest, MetadataWithoutEnvironmentHasStableIds) {
  const auto config_file = test_dir / "apps.json";
  std::ofstream(config_file) << R"({"apps":[{"name":"One"},{"name":"One"}]})";
  auto registry = proc::parse(config_file.string());
  ASSERT_TRUE(registry);
  ASSERT_EQ(registry->get_apps().size(), 2);
  EXPECT_EQ(registry->get_apps()[0].id, std::get<0>(proc::calculate_app_id("One", "", 0)));
  EXPECT_EQ(registry->get_apps()[1].id, std::get<1>(proc::calculate_app_id("One", "", 1)));
  EXPECT_NE(registry->get_apps()[0].id, registry->get_apps()[1].id);
}

/** @brief Invalid metadata remains an error instead of an empty successful parse. */
TEST_F(ProcessPNGTest, InvalidApplicationMetadataFailsParsing) {
  const auto config_file = test_dir / "apps.json";
  std::ofstream(config_file) << R"({"apps":[{}]})";
  EXPECT_FALSE(proc::parse(config_file.string()));
  std::ofstream(config_file) << "{";
  EXPECT_FALSE(proc::parse(config_file.string()));
}
