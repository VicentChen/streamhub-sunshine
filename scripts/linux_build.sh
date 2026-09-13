#!/bin/bash
set -e

# Version requirements - centralized for easy maintenance
cmake_min="4.0.0"
target_cmake_version="4.3.0"
doxygen_min="1.10.0"
_doxygen_min="${doxygen_min//\./_}"  # Convert dots to underscores for URL
doxygen_max="1.12.0"

# Default value for arguments
appimage_build=0
num_processors=$(nproc)
publisher_name="Third Party Publisher"
publisher_website=""
publisher_issue_url="https://app.lizardbyte.dev/support"
skip_cleanup=0
skip_package=0
sudo_cmd="sudo"
ubuntu_test_repo=0
step="all"

# constants
AARCH64="aarch64"
DOXYGEN="doxygen"

function setup_nvm_environment() {
  # Only setup NVM if it should be used for this distro
  if [[ "$nvm_node" == 1 ]]; then
    # Check if NVM is installed and source it
    if [[ -f "$HOME/.nvm/nvm.sh" ]]; then
      # shellcheck source=/dev/null
      source "$HOME/.nvm/nvm.sh"
      # Use the default node version installed by NVM
      nvm use default 2>/dev/null || nvm use node 2>/dev/null || true
      echo "Using NVM Node.js version: $(node --version 2>/dev/null || echo 'not available')"
      echo "Using NVM npm version: $(npm --version 2>/dev/null || echo 'not available')"
    else
      echo "NVM not found, using system Node.js if available"
    fi
  fi
  return 0
}

function _usage() {
  local exit_code=$1

  cat <<EOF
This script installs the dependencies and builds the project.
The script is intended to be run on a Debian-based or Fedora-based system.

Usage:
  $0 [options]

Options:
  -h, --help               Display this help message.
  -s, --sudo-off           Disable sudo command.
  --appimage-build         Compile for AppImage, this will not create the AppImage, just the executable.
  --num-processors         The number of processors to use for compilation. Default is the value of 'nproc'.
  --publisher-name         The name of the publisher (not developer) of the application.
  --publisher-website      The URL of the publisher's website.
  --publisher-issue-url    The URL of the publisher's support site or issue tracker.
                           If you provide a modified version of Sunshine, we kindly request that you use your own url.
  --skip-cleanup           Do not restore the original gcc alternatives, or the math-vector.h file.
  --skip-package           Skip creating DEB, or RPM package.
  --ubuntu-test-repo       Install ppa:ubuntu-toolchain-r/test repo on Ubuntu.
  --step                   Which step(s) to run: deps, cmake, validation, build, package, cleanup, or all (default: all)

Steps:
  deps                     Install dependencies only
  cmake                    Run cmake configure only
  validation               Run validation commands only
  build                    Build the project only
  package                  Create packages only
  cleanup                  Cleanup alternatives and backups only
  all                      Run all steps (default)
EOF

  exit "$exit_code"
}

# Parse named arguments
while getopts ":hs-:" opt; do
  case ${opt} in
    h ) _usage 0 ;;
    s ) sudo_cmd="" ;;
    - )
      case "${OPTARG}" in
        help) _usage 0 ;;
        appimage-build)
          appimage_build=1
          ;;
        num-processors=*)
          num_processors="${OPTARG#*=}"
          ;;
        publisher-name=*)
          publisher_name="${OPTARG#*=}"
          ;;
        publisher-website=*)
          publisher_website="${OPTARG#*=}"
          ;;
        publisher-issue-url=*)
          publisher_issue_url="${OPTARG#*=}"
          ;;
        skip-cleanup) skip_cleanup=1 ;;
        skip-package) skip_package=1 ;;
        sudo-off) sudo_cmd="" ;;
        ubuntu-test-repo) ubuntu_test_repo=1 ;;
        step=*)
          step="${OPTARG#*=}"
          ;;
        *)
          echo "Invalid option: --${OPTARG}" 1>&2
          _usage 1
          ;;
      esac
      ;;
    \? )
      echo "Invalid option: -${OPTARG}" 1>&2
      _usage 1
      ;;
  esac
done
shift $((OPTIND -1))

# dependencies array to build out
dependencies=()

function add_arch_deps() {
  dependencies+=(
    'appstream-glib'
    'avahi'
    'base-devel'
    'cmake'
    'curl'
    'doxygen'
    "gcc${gcc_version}"
    "gcc${gcc_version}-libs"
    'git'
    'graphviz'
    'glib2'
    'libcap'
    'miniupnpc'
    'ninja'
    'nodejs'
    'npm'
    'openssl'
    'opus'
  )
  return 0
}

function add_debian_based_deps() {
  dependencies+=(
    "appstream"
    "appstream-util"
    "bison"  # required if we need to compile doxygen
    "build-essential"
    "cmake"
    "desktop-file-utils"
    "${DOXYGEN}"
    "file"
    "flex"  # required if we need to compile doxygen
    "gcc-${gcc_version}"
    "g++-${gcc_version}"
    "git"
    "graphviz"
    "libcurl4-openssl-dev"
    "libglib2.0-dev"
    "libminiupnpc-dev"
    "libopus-dev"
    "libssl-dev"
    "libsystemd-dev"
    "ninja-build"
    "npm"  # web-ui
    "systemd"
    "wget"
  )
  return 0
}

function add_test_ppa() {
  if [[ "$ubuntu_test_repo" == 1 ]]; then
    $package_install_command "software-properties-common"
    ${sudo_cmd} add-apt-repository ppa:ubuntu-toolchain-r/test -y
  fi
  return 0
}

function add_debian_deps() {
  add_test_ppa
  add_debian_based_deps
  dependencies+=(
    "systemd-dev"
  )
  return 0
}

function add_ubuntu_deps() {
  # Enable universe for the required development packages.
  $package_install_command "software-properties-common"
  ${sudo_cmd} add-apt-repository universe -y
  add_test_ppa
  add_debian_based_deps

  if [[ "$(printf '%s\n' "$version" "24.04" | sort -V | head -n1)" == "24.04" ]]; then
    dependencies+=(
      "systemd-dev"
    )
  fi
  return 0
}

function add_fedora_deps() {
  dependencies+=(
    "appstream"
    "cmake"
    "desktop-file-utils"
    "${DOXYGEN}"
    "gcc${gcc_version}"
    "gcc${gcc_version}-c++"
    "git"
    "graphviz"
    "libappstream-glib"
    "libcurl-devel"
    "glib2-devel"
    "miniupnpc-devel"
    "ninja-build"
    "npm"
    "openssl-devel"
    "opus-devel"
    "rpm-build"  # if you want to build an RPM binary package
    "wget"
    "which"
  )
  return 0
}

function check_version() {
  local package_name=$1
  local min_version=$2
  local max_version=$3
  local installed_version

  echo "Checking if $package_name is installed and at least version $min_version"

  if [[ "$distro" == "debian" ]] || [[ "$distro" == "ubuntu" ]]; then
    installed_version=$(dpkg -s "$package_name" 2>/dev/null | grep '^Version:' | awk '{print $2}')
  elif [[ "$distro" == "fedora" ]]; then
    installed_version=$(rpm -q --queryformat '%{VERSION}' "$package_name" 2>/dev/null)
  elif [[ "$distro" == "arch" ]]; then
    installed_version=$(pacman -Q "$package_name" | awk '{print $2}' )
  else
    echo "Unsupported Distro"
    return 1
  fi

  if [[ -z "$installed_version" ]]; then
    echo "Package not installed"
    return 1
  fi

if [[ "$(printf '%s\n' "$installed_version" "$min_version" | sort -V | head -n1)" = "$min_version" ]] && \
   [[ "$(printf '%s\n' "$installed_version" "$max_version" | sort -V | head -n1)" = "$installed_version" ]]; then
    echo "Installed version is within range"
    return 0
  else
    echo "$package_name version $installed_version is out of range"
    return 1
  fi
}

function run_step_deps() {
  echo "Running step: Install dependencies"

  # Update the package list
  $package_update_command

  if [[ "$distro" == "arch" ]]; then
    add_arch_deps
  elif [[ "$distro" == "debian" ]]; then
    add_debian_deps
  elif [[ "$distro" == "ubuntu" ]]; then
    add_ubuntu_deps
  elif [[ "$distro" == "fedora" ]]; then
    add_fedora_deps
    ${sudo_cmd} dnf group install "development-tools" -y
  fi

  # Install the dependencies
  $package_install_command "${dependencies[@]}"

  # reload the environment
  # shellcheck source=/dev/null
  source ~/.bashrc

  #set gcc version based on distros
  export CC=gcc-${gcc_version}
  export CXX=g++-${gcc_version}

  # compile cmake if the version is too low
  if ! check_version "cmake" "$cmake_min" "inf"; then
    cmake_prefix="https://github.com/Kitware/CMake/releases/download/v"
    if [[ "$architecture" == "x86_64" ]]; then
      cmake_arch="x86_64"
    elif [[ "$architecture" == "${AARCH64}" ]]; then
      cmake_arch="${AARCH64}"
    fi
    url="${cmake_prefix}${target_cmake_version}/cmake-${target_cmake_version}-linux-${cmake_arch}.sh"
    echo "cmake url: ${url}"
    wget "$url" --progress=bar:force:noscroll -q --show-progress -O "${build_dir}/cmake.sh"
    ${sudo_cmd} sh "${build_dir}/cmake.sh" --skip-license --prefix=/usr/local
    echo "cmake installed, version:"
    cmake --version
  fi

  # compile doxygen if version is too low
  if ! check_version "${DOXYGEN}" "$doxygen_min" "$doxygen_max"; then
    if [[ "${SUNSHINE_COMPILE_DOXYGEN}" == "true" ]]; then
      echo "Compiling doxygen"
      doxygen_url="https://github.com/doxygen/doxygen/releases/download/Release_${_doxygen_min}/${DOXYGEN}-${doxygen_min}.src.tar.gz"
      echo "${DOXYGEN} url: ${doxygen_url}"
      pushd "${build_dir}"
        local -a doxygen_download_args=(
          "$doxygen_url"
          --max-redirect=1
          --progress=bar:force:noscroll
          -q
          --show-progress
          -O "${DOXYGEN}.tar.gz"
        )
        # GitHub release downloads require one redirect to the release asset host.
        wget "${doxygen_download_args[@]}"  # NOSONAR(shell:S6506)
        tar -xzf "${DOXYGEN}.tar.gz"
        cd "${DOXYGEN}-${doxygen_min}"
        cmake -DCMAKE_BUILD_TYPE=Release -G="Ninja" -B="build" -S="."
        ninja -C "build" -j"${num_processors}"
        ${sudo_cmd} ninja -C "build" install
      popd
    else
      echo "${DOXYGEN} version not in range, skipping docs"
      # Note: cmake_args will be set in cmake step
    fi
  fi

  # install node from nvm
  if [[ "$nvm_node" == 1 ]]; then
    nvm_url="https://raw.githubusercontent.com/nvm-sh/nvm/master/install.sh"
    echo "nvm url: ${nvm_url}"
    wget --max-redirect=0 -qO- ${nvm_url} | bash

    # shellcheck source=/dev/null  # we don't care that shellcheck cannot find nvm.sh
    source "$HOME/.nvm/nvm.sh"
    nvm install node
    nvm use node
  fi

  return 0
}

function run_step_cmake() {
  echo "Running step: CMake configure"

  # Setup NVM environment if needed (for web UI builds)
  setup_nvm_environment


  #set gcc version based on distros
  export CC=gcc-${gcc_version}
  export CXX=g++-${gcc_version}

  # prepare CMAKE args
  cmake_args=(
    "-B=build"
    "-G=Ninja"
    "-S=."
    "-DBUILD_WERROR=ON"
    "-DCMAKE_BUILD_TYPE=Release"
    "-DCMAKE_INSTALL_PREFIX=/usr"
    "-DSUNSHINE_ASSETS_DIR=share/sunshine"
    "-DSUNSHINE_EXECUTABLE_PATH=/usr/bin/sunshine"
  )

  if [[ "$appimage_build" == 1 ]]; then
    cmake_args+=("-DSUNSHINE_BUILD_APPIMAGE=ON")
  fi

  # Publisher metadata
  if [[ -n "$publisher_name" ]]; then
    cmake_args+=("-DSUNSHINE_PUBLISHER_NAME='${publisher_name}'")
  fi
  if [[ -n "$publisher_website" ]]; then
    cmake_args+=("-DSUNSHINE_PUBLISHER_WEBSITE='${publisher_website}'")
  fi
  if [[ -n "$publisher_issue_url" ]]; then
    cmake_args+=("-DSUNSHINE_PUBLISHER_ISSUE_URL='${publisher_issue_url}'")
  fi

  # Handle doxygen docs flag
  if ! check_version "${DOXYGEN}" "$doxygen_min" "$doxygen_max" && [[ "${SUNSHINE_COMPILE_DOXYGEN}" != "true" ]]; then
    cmake_args+=("-DBUILD_DOCS=OFF")
  fi


  # Cmake stuff here
  mkdir -p "build"
  echo "cmake args:"
  echo "${cmake_args[@]}"
  cmake "${cmake_args[@]}"
  return 0
}

function run_step_validation() {
  echo "Running step: Validation"

  # Run appstream validation, etc.
  appstreamcli validate "build/dev.lizardbyte.app.Sunshine.metainfo.xml"
  appstream-util validate "build/dev.lizardbyte.app.Sunshine.metainfo.xml"
  desktop-file-validate "build/dev.lizardbyte.app.Sunshine.desktop"
  if [[ "$appimage_build" == 0 ]]; then
    desktop-file-validate "build/dev.lizardbyte.app.Sunshine.terminal.desktop"
  fi
  return 0
}

function run_step_build() {
  echo "Running step: Build"

  # Setup NVM environment if needed (for web UI builds)
  setup_nvm_environment

  # Build the project
  ninja -C "build"
  return 0
}

function run_step_package() {
  echo "Running step: Package"

  # Create the package
  if [[ "$skip_package" == 0 ]]; then
    if [[ "$distro" == "debian" ]] || [[ "$distro" == "ubuntu" ]]; then
      cpack -G DEB --config ./build/CPackConfig.cmake
    elif [[ "$distro" == "fedora" ]]; then
      cpack -G RPM --config ./build/CPackConfig.cmake
    fi
  fi
  return 0
}

function run_step_cleanup() {
  echo "Running step: Cleanup"

  # restore the math-vector.h file
  if [[ "$skip_cleanup" == 0 ]] && [[ "$architecture" == "${AARCH64}" ]] && [[ -n "$math_vector_file" ]]; then
    ${sudo_cmd} mv -f "$math_vector_file.bak" "$math_vector_file"
  fi
  return 0
}

function run_install() {
  case "$step" in
    deps)
      run_step_deps
      ;;
    cmake)
      run_step_cmake
      ;;
    validation)
      run_step_validation
      ;;
    build)
      run_step_build
      ;;
    package)
      run_step_package
      ;;
    cleanup)
      run_step_cleanup
      ;;
    all)
      run_step_deps
      run_step_cmake
      run_step_validation
      run_step_build
      run_step_package
      run_step_cleanup
      ;;
    *)
      echo "Invalid step: $step"
      echo "Valid steps are: deps, cmake, validation, build, package, cleanup, all"
      exit 1
      ;;
  esac
  return 0
}

# Determine the OS and call the appropriate function
cat /etc/os-release

if grep -q "Arch Linux" /etc/os-release; then
  distro="arch"
  version=""
  package_update_command="${sudo_cmd} pacman -Syu --noconfirm"
  package_install_command="${sudo_cmd} pacman -Sy --needed"
  nvm_node=0
  gcc_version="14"
elif grep -q "Debian GNU/Linux 12 (bookworm)" /etc/os-release; then
  distro="debian"
  version="12"
  package_update_command="${sudo_cmd} apt-get update"
  package_install_command="${sudo_cmd} apt-get install -y"
  gcc_version="13"
  nvm_node=0
elif grep -q "Debian GNU/Linux 13 (trixie)" /etc/os-release; then
  distro="debian"
  version="13"
  package_update_command="${sudo_cmd} apt-get update"
  package_install_command="${sudo_cmd} apt-get install -y"
  gcc_version="14"
  nvm_node=0
elif grep -q "PLATFORM_ID=\"platform:f42\"" /etc/os-release; then
  distro="fedora"
  version="42"
  package_update_command="${sudo_cmd} dnf update -y"
  package_install_command="${sudo_cmd} dnf install -y"
  gcc_version="14"
  nvm_node=0
elif grep -q '^ID=fedora$' /etc/os-release && grep -q '^VERSION_ID=43$' /etc/os-release; then
  distro="fedora"
  version="43"
  package_update_command="${sudo_cmd} dnf update -y"
  package_install_command="${sudo_cmd} dnf install -y"
  gcc_version="14"
  nvm_node=0
elif grep -q '^ID=fedora$' /etc/os-release && grep -q '^VERSION_ID=44$' /etc/os-release; then
  distro="fedora"
  version="44"
  package_update_command="${sudo_cmd} dnf update -y"
  package_install_command="${sudo_cmd} dnf install -y"
  gcc_version="14"
  nvm_node=0
elif grep -q '^ID=fedora$' /etc/os-release && grep -q '^VERSION_ID=45$' /etc/os-release; then
  distro="fedora"
  version="45"
  package_update_command="${sudo_cmd} dnf update -y"
  package_install_command="${sudo_cmd} dnf install -y"
  gcc_version="15"
  nvm_node=0
elif grep -q "Ubuntu 22.04" /etc/os-release; then
  distro="ubuntu"
  version="22.04"
  package_update_command="${sudo_cmd} apt-get update"
  package_install_command="${sudo_cmd} apt-get install -y"
  gcc_version="14"
  nvm_node=1
elif grep -q "Ubuntu 24.04" /etc/os-release; then
  distro="ubuntu"
  version="24.04"
  package_update_command="${sudo_cmd} apt-get update"
  package_install_command="${sudo_cmd} apt-get install -y"
  gcc_version="14"
  nvm_node=1
elif grep -q "Ubuntu 25.04" /etc/os-release; then
  distro="ubuntu"
  version="25.04"
  package_update_command="${sudo_cmd} apt-get update"
  package_install_command="${sudo_cmd} apt-get install -y"
  gcc_version="14"
  nvm_node=0
elif grep -q "Ubuntu 25.10" /etc/os-release; then
  distro="ubuntu"
  version="25.10"
  package_update_command="${sudo_cmd} apt-get update"
  package_install_command="${sudo_cmd} apt-get install -y"
  gcc_version="14"
  nvm_node=0
elif grep -q 'VERSION_ID="26.04"' /etc/os-release; then
  distro="ubuntu"
  version="26.04"
  package_update_command="${sudo_cmd} apt-get update"
  package_install_command="${sudo_cmd} apt-get install -y"
  gcc_version="14"
  nvm_node=0
else
  echo "Unsupported Distro or Version"
  exit 1
fi

architecture=$(uname -m)

echo "Detected Distro: $distro"
echo "Detected Version: $version"
echo "Detected Architecture: $architecture"

if [[ "$architecture" != "x86_64" ]] && [[ "$architecture" != "${AARCH64}" ]]; then
  echo "Unsupported Architecture"
  exit 1
fi

# export variables for github actions ci
if [[ -f "$GITHUB_ENV" ]]; then
  {
    echo "CC=gcc-${gcc_version}"
    echo "CXX=g++-${gcc_version}"
    echo "GCC_VERSION=${gcc_version}"
  } >> "$GITHUB_ENV"
fi

# get directory of this script
script_dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
build_dir=$(readlink -f "$script_dir/../build")
echo "Script Directory: $script_dir"
echo "Build Directory: $build_dir"
mkdir -p "$build_dir"

run_install
