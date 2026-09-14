cmake_minimum_required(VERSION 3.24)
# `set_source_files_properties` requires 3.18
# `cmake_path(CONVERT ... TO_NATIVE_PATH_LIST ...)` requires 3.20
# todo - set this conditionally


# Product builds must not create artifacts outside the product checkout.
if(DEFINED ENV{STREAMHUB_ROOT})
  file(REAL_PATH "$ENV{STREAMHUB_ROOT}" STREAMHUB_PRODUCT_ROOT)
else()
  file(REAL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/.." STREAMHUB_PRODUCT_ROOT)
endif()
file(REAL_PATH "${CMAKE_BINARY_DIR}" STREAMHUB_BUILD_ROOT)
cmake_path(IS_PREFIX STREAMHUB_PRODUCT_ROOT "${STREAMHUB_BUILD_ROOT}" NORMALIZE STREAMHUB_BUILD_INSIDE)
if(STREAMHUB_PRODUCT_ROOT STREQUAL "/" OR NOT STREAMHUB_BUILD_INSIDE OR STREAMHUB_BUILD_ROOT STREQUAL STREAMHUB_PRODUCT_ROOT)
  message(FATAL_ERROR "Build output must be below the StreamHub product root")
endif()

project(Sunshine VERSION 0.0.0
        DESCRIPTION "Self-hosted game stream host for Moonlight"
        HOMEPAGE_URL "https://app.lizardbyte.dev/Sunshine")


set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PROJECT_LICENSE "GPL-3.0-only")

set(PROJECT_FQDN "dev.lizardbyte.app.Sunshine")

set(PROJECT_BRIEF_DESCRIPTION "GameStream host for Moonlight")  # must be <= 35 characters

set(PROJECT_LONG_DESCRIPTION "Moonlight protocol core for StreamHub, with pairing, sessions and media transport.")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Setting build type to 'Release' as none was specified.")
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
    set(FREEBSD ON)
endif()

# set the module path, used for includes
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# CPM
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/cpm/CPM.cmake)
CPMUsePackageLock(package-lock.cmake)

# export compile_commands.json
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# set version info for this build
include(${CMAKE_MODULE_PATH}/prep/build_version.cmake)

# cmake build flags
include(${CMAKE_MODULE_PATH}/prep/options.cmake)

# initial prep
include(${CMAKE_MODULE_PATH}/prep/init.cmake)

# configure special package files, such as sunshine.desktop, Flatpak manifest, Portfile , etc.
include(${CMAKE_MODULE_PATH}/prep/special_package_configuration.cmake)

# Exit early if END_BUILD is ON, i.e. when only generating package manifests
if(${END_BUILD})
    return()
endif()

# project constants
include(${CMAKE_MODULE_PATH}/prep/constants.cmake)

# load macros
include(${CMAKE_MODULE_PATH}/macros/common.cmake)

# load dependencies
include(${CMAKE_MODULE_PATH}/dependencies/common.cmake)

# setup compile definitions
include(${CMAKE_MODULE_PATH}/compile_definitions/common.cmake)

# target definitions
include(${CMAKE_MODULE_PATH}/targets/common.cmake)

# packaging
include(${CMAKE_MODULE_PATH}/packaging/common.cmake)
