require "language/node"

class Sunshine < Formula

  COVERAGE_LCOV = "coverage.lcov".freeze
  COVERAGE_PROFDATA = "coverage.profdata".freeze
  COVERAGE_XML = "coverage.xml".freeze
  GCC_VERSION = "14".freeze
  GCC_FORMULA = "gcc@#{GCC_VERSION}".freeze
  LLVM_PROFILE_FILE_ENV = "LLVM_PROFILE_FILE".freeze
  TEST_BINARY = "test_sunshine".freeze
  IS_UPSTREAM_REPO = ENV.fetch("GITHUB_REPOSITORY", "") == "LizardByte/Sunshine"

  desc "@PROJECT_DESCRIPTION@"
  homepage "@PROJECT_HOMEPAGE_URL@"
  url "@GITHUB_CLONE_URL@",
    tag: "@GITHUB_TAG@"
  version "@BUILD_VERSION@"
  license all_of: ["GPL-3.0-only"]
  head "@GITHUB_CLONE_URL@", branch: "@GITHUB_DEFAULT_BRANCH@"

  # https://docs.brew.sh/Brew-Livecheck#githublatest-strategy-block
  livecheck do
    url :stable
    regex(/^v?(\d+\.\d+\.\d+)$/i)
    strategy :github_latest do |json, regex|
      match = json["tag_name"]&.match(regex)
      next if match.blank?

      match[1]
    end
  end

  bottle do
    root_url "https://ghcr.io/v2/lizardbyte/homebrew"
    sha256 arm64_tahoe:   "0000000000000000000000000000000000000000000000000000000000000000"
    sha256 arm64_sequoia: "0000000000000000000000000000000000000000000000000000000000000000"
    sha256 arm64_sonoma:  "0000000000000000000000000000000000000000000000000000000000000000"
    sha256 arm64_linux:   "0000000000000000000000000000000000000000000000000000000000000000"
    sha256 x86_64_linux:  "0000000000000000000000000000000000000000000000000000000000000000"
  end

  option "with-docs", "Enable docs build"
  option "with-static-boost", "Enable static link of Boost libraries"
  option "without-static-boost", "Disable static link of Boost libraries" # default option

  depends_on "cmake" => :build
  depends_on "doxygen" => :build if build.with? "docs"
  depends_on "graphviz" => :build if build.with? "docs"
  depends_on "node" => :build
  depends_on "pkgconf" => :build
  depends_on "boost"
  depends_on "curl"
  depends_on "icu4c@78"
  depends_on "miniupnpc"
  depends_on "openssl@3"
  depends_on "opus"

  on_sonoma do
    depends_on xcode: ["16.2", :build] # required for jthreads on macos-14
  end

  on_linux do
    depends_on GCC_FORMULA => [:build, :test]
    depends_on "gcovr" => [:build, :test]
    depends_on "python3" => :build
    depends_on "imagemagick" => :test
    depends_on "avahi"
    depends_on "glib"
    depends_on "gnu-which"
    depends_on "libcap"
    depends_on "systemd"

  end

  conflicts_with "sunshine-beta", because: "sunshine and sunshine-beta cannot be installed at the same time"

  fails_with :clang do
    build 1400
    cause "Requires C++23 support"
  end

  fails_with :gcc do
    version "12" # fails with GCC 12.x and earlier
    cause "Requires C++23 support"
  end


  def setup_build_environment
    ENV["BRANCH"] = "@GITHUB_BRANCH@"
    ENV["BUILD_VERSION"] = "@BUILD_VERSION@"
    ENV["COMMIT"] = "@GITHUB_COMMIT@"

    setup_linux_gcc_environment if OS.linux?

  end

  def setup_linux_gcc_environment
    # Use GCC because gcov from llvm cannot handle our paths
    gcc_path = Formula[GCC_FORMULA]
    ENV["CC"] = "#{gcc_path.opt_bin}/gcc-#{GCC_VERSION}"
    ENV["CXX"] = "#{gcc_path.opt_bin}/g++-#{GCC_VERSION}"
  end

  def base_cmake_args
    args = %W[
      -DBUILD_WERROR=ON
      -DCMAKE_INSTALL_PREFIX=#{prefix}
      -DHOMEBREW_ALLOW_FETCHCONTENT=ON
      -DOPENSSL_ROOT_DIR=#{formula_opt_prefix("openssl")}
      -DSUNSHINE_ASSETS_DIR=sunshine/assets
      -DSUNSHINE_BUILD_HOMEBREW=ON
      -DSUNSHINE_PUBLISHER_NAME='LizardByte'
      -DSUNSHINE_PUBLISHER_WEBSITE='https://app.lizardbyte.dev'
      -DSUNSHINE_PUBLISHER_ISSUE_URL='https://app.lizardbyte.dev/support'
    ]
    args << "-DSUNSHINE_EXECUTABLE_PATH=#{opt_bin}/sunshine" if OS.linux?
    # Point cmake at the venv Python that has jinja2 installed (set up in setup_build_environment)
    args
  end

  def add_test_args(args)
    if IS_UPSTREAM_REPO
      args << "-DBUILD_TESTS=ON"
      args << "-DSUNSHINE_LLVM_COVERAGE=ON" if OS.mac?
      ohai "Building tests: enabled"
    else
      args << "-DBUILD_TESTS=OFF"
      ohai "Building tests: disabled"
    end
  end

  def add_docs_args(args)
    if build.with? "docs"
      ohai "Building docs: enabled"
      args << "-DBUILD_DOCS=ON"
    else
      ohai "Building docs: disabled"
      args << "-DBUILD_DOCS=OFF"
    end
  end

  def add_boost_args(args)
    if build.without? "static-boost"
      args << "-DBOOST_USE_STATIC=OFF"
      ohai "Disabled statically linking Boost libraries"
    else
      configure_static_boost(args)
    end
  end

  def configure_static_boost(args)
    args << "-DBOOST_USE_STATIC=ON"
    ohai "Enabled statically linking Boost libraries"

    unless formula_any_version_installed?("icu4c")
      odie <<~EOS
        icu4c must be installed to link against static Boost libraries,
        either install icu4c or use brew install sunshine --with-static-boost instead
      EOS
    end
    ENV.append "CXXFLAGS", "-I#{formula_opt_include("icu4c")}"
    icu4c_lib_path = formula_opt_lib("icu4c").to_s
    ENV.append "LDFLAGS", "-L#{icu4c_lib_path}"
    ENV["LIBRARY_PATH"] = icu4c_lib_path
    ohai "Linking against ICU libraries at: #{icu4c_lib_path}"
  end


  def release_homebrew_testpath
    testpath_value = ENV.fetch("HOMEBREW_TEST_ARTIFACTS_DIR", "")
    return Pathname.new(testpath_value) unless testpath_value.empty?

    return if ENV.fetch("HOMEBREW_TEST_BOT", "") != "1"

    temp_path = ENV.fetch("HOMEBREW_TEMP", "")
    return if temp_path.empty?

    Pathname.new(temp_path)/name/"test"
  end

  def ensure_artifact_exists(path)
    odie "#{path} was not created" unless path.exist?
  end

  def run_test_suite(artifact_dir)
    mkdir_p artifact_dir/"tests"
    test_results = artifact_dir/"tests/test_results.xml"

    if OS.mac?
      with_llvm_profile_file(artifact_dir) do
        system bin/TEST_BINARY, "--gtest_color=yes", "--gtest_output=xml:#{test_results}"
      end
    else
      system bin/TEST_BINARY, "--gtest_color=yes", "--gtest_output=xml:#{test_results}"
    end

    ensure_artifact_exists test_results
  end

  def with_llvm_profile_file(artifact_dir)
    original_profile_file = ENV.fetch(LLVM_PROFILE_FILE_ENV, nil)
    ENV[LLVM_PROFILE_FILE_ENV] = "#{artifact_dir}/sunshine-%p.profraw"
    yield
  ensure
    if original_profile_file
      ENV[LLVM_PROFILE_FILE_ENV] = original_profile_file
    else
      ENV.delete(LLVM_PROFILE_FILE_ENV)
    end
  end

  def coverage_gcov_options
    gcc_path = Formula[GCC_FORMULA]
    ["--gcov-executable", "#{gcc_path.opt_bin}/gcov-#{GCC_VERSION}"]
  end

  def llvm_profdata_executable
    Utils.safe_popen_read("xcrun", "--find", "llvm-profdata").strip
  end

  def llvm_cov_executable
    Utils.safe_popen_read("xcrun", "--find", "llvm-cov").strip
  end

  def coverage_report_path(artifact_dir)
    artifact_dir/(OS.mac? ? COVERAGE_LCOV : COVERAGE_XML)
  end

  def coverage_common_options(coverage_report)
    [
      "--exclude-noncode-lines",
      "--exclude-throw-branches",
      "--exclude-unreachable-branches",
      "--xml-pretty",
      "-o=#{coverage_report}",
    ]
  end

  def generate_coverage_report(artifact_dir, coverage_buildpath)
    return if coverage_buildpath.to_s.empty?

    coverage_report = coverage_report_path(artifact_dir)

    if OS.mac?
      generate_llvm_coverage_report artifact_dir, coverage_buildpath, coverage_report
    else
      generate_gcov_coverage_report coverage_report, coverage_buildpath
    end

    ensure_artifact_exists coverage_report
  end

  def generate_llvm_coverage_report(artifact_dir, coverage_buildpath, coverage_report)
    profile_files = Dir["#{artifact_dir}/*.profraw"]
    odie "No LLVM profile data was created" if profile_files.empty?

    profile_data = artifact_dir/COVERAGE_PROFDATA
    system llvm_profdata_executable, "merge", "--sparse", "-o", profile_data.to_s, *profile_files
    lcov = Utils.safe_popen_read(
      llvm_cov_executable,
      "export",
      "-format=lcov",
      "-instr-profile=#{profile_data}",
      (bin/TEST_BINARY).to_s,
    )
    coverage_report.write lcov_for_source_files(lcov, coverage_buildpath)
    ensure_lcov_report_has_lines coverage_report
  end

  def generate_gcov_coverage_report(coverage_report, coverage_buildpath)
    cd "#{coverage_buildpath}/build" do
      system "gcovr", ".",
        "-r", "../src",
        *coverage_gcov_options,
        *coverage_common_options(coverage_report)
    end

    ensure_cobertura_report_has_lines coverage_report
  end

  def coverage_source_prefixes(coverage_buildpath)
    paths = [
      coverage_buildpath.to_s,
      Pathname.new(coverage_buildpath.to_s).realpath.to_s,
    ]
    paths.uniq.map { |path| "#{path}/src/" }
  end

  def relative_lcov_record(record, source_prefixes)
    lines = record.lines
    source_index = lines.index { |line| line.start_with?("SF:") }
    return unless source_index

    source_path = Pathname.new(lines[source_index].delete_prefix("SF:").strip).cleanpath.to_s
    # Homebrew remaps the formula build path to ".". LLVM may then resolve that
    # relative path from CMake's compilation directory at "build/tests".
    relative_source_path = if source_path.start_with?("build/tests/src/")
      source_path.delete_prefix("build/tests/")
    elsif source_path.start_with?("src/")
      source_path
    else
      source_prefix = source_prefixes.find { |prefix| source_path.start_with?(prefix) }
      "src/#{source_path.delete_prefix(source_prefix)}" if source_prefix
    end
    return unless relative_source_path

    lines[source_index] = "SF:#{relative_source_path}\n"
    "#{lines.join}end_of_record\n"
  end

  def lcov_for_source_files(lcov, coverage_buildpath)
    source_prefixes = coverage_source_prefixes(coverage_buildpath)
    records = lcov.split("end_of_record\n")
    records.filter_map { |record| relative_lcov_record(record, source_prefixes) }.join
  end

  def ensure_cobertura_report_has_lines(path)
    lines_valid = path.read[/lines-valid="(\d+)"/, 1].to_i
    odie "#{path} does not contain any source lines" if lines_valid.zero?
  end

  def ensure_lcov_report_has_lines(path)
    has_lines = path.read.lines.any? { |line| line.start_with?("DA:") }
    odie "#{path} does not contain any source lines" unless has_lines
  end

  def collect_test_artifacts
    artifact_dir = release_homebrew_testpath
    return unless IS_UPSTREAM_REPO
    return unless artifact_dir

    run_test_suite artifact_dir
    generate_coverage_report artifact_dir, buildpath
  end

  def build_cmake_args
    args = base_cmake_args
    add_test_args(args)
    add_docs_args(args)
    add_boost_args(args)
    args
  end

  def build_and_install_project
    system "cmake", "-S", ".", "-B", "build", "-G", "Unix Makefiles",
            *std_cmake_args,
            *build_cmake_args

    system "make", "-C", "build"
    system "make", "-C", "build", "install"
  end

  def install_platform_specific_files
    bin.install "build/tests/#{TEST_BINARY}" if IS_UPSTREAM_REPO

    # codesign the binary on intel macs
    system "codesign", "-s", "-", "--force", "--deep", bin/"sunshine" if OS.mac? && Hardware::CPU.intel?

    bin.install "src_assets/linux/misc/postinst" if OS.linux?
  end

  def install
    setup_build_environment
    build_and_install_project
    install_platform_specific_files
    collect_test_artifacts
  end

  service do
    run [opt_bin/"sunshine", "~/.config/sunshine/sunshine.conf"] if OS.mac?
    name linux: "app-@PROJECT_FQDN@" if OS.linux?
  end

  post_install_steps do
    on_linux do
      warn <<~EOS
        ATTENTION: To complete installation, you must run the following command:
        `sudo {{bin}}/postinst`
      EOS
    end

    on_macos do
      warn <<~EOS
        Gamepads are not currently supported on macOS.
      EOS
    end
  end

  def caveats
    <<~EOS
      Thanks for installing @PROJECT_NAME@!

      To get started, review the documentation at:
        https://docs.lizardbyte.dev/projects/sunshine
    EOS
  end

  test do
    # test that the binary runs at all
    system bin/"sunshine", "--version"

    if OS.linux?
    end

    if IS_UPSTREAM_REPO
      artifact_dir = release_homebrew_testpath
      if artifact_dir
        assert_path_exists artifact_dir/"tests/test_results.xml"
        assert_path_exists coverage_report_path(artifact_dir)
      elsif ENV.fetch("HOMEBREW_BOTTLE_BUILD", "false") != "true"
        run_test_suite testpath
        generate_coverage_report testpath, ENV.fetch("HOMEBREW_BUILDPATH", "")
      end

      lcov = <<~LCOV
        SF:./src/remapped.cpp
        DA:1,1
        end_of_record
        SF:build/tests/src/remapped_from_compile_dir.cpp
        DA:1,1
        end_of_record
        SF:src/relative.cpp
        DA:1,1
        end_of_record
        SF:#{testpath}/src/absolute.cpp
        DA:1,1
        end_of_record
        SF:./tests/excluded.cpp
        DA:1,1
        end_of_record
        SF:build/tests/tests/excluded_from_compile_dir.cpp
        DA:1,1
        end_of_record
      LCOV
      expected_lcov = <<~LCOV
        SF:src/remapped.cpp
        DA:1,1
        end_of_record
        SF:src/remapped_from_compile_dir.cpp
        DA:1,1
        end_of_record
        SF:src/relative.cpp
        DA:1,1
        end_of_record
        SF:src/absolute.cpp
        DA:1,1
        end_of_record
      LCOV
      assert_equal expected_lcov, lcov_for_source_files(lcov, testpath)
    end
  end
end
