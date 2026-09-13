# linux specific compile definitions

if(FREEBSD)
    add_compile_definitions(SUNSHINE_PLATFORM="freebsd")
    # FreeBSD installs packages to /usr/local/lib, which is not in the default linker search path.
    # link_directories() is directory-scoped and propagates to all subdirectories (including tests/),
    # so all targets (sunshine, test_sunshine) can resolve libraries found via pkg_check_modules.
    link_directories(/usr/local/lib)
else()
    add_compile_definitions(SUNSHINE_PLATFORM="linux")
endif()

# AppImage
if(${SUNSHINE_BUILD_APPIMAGE})
    # use relative assets path for AppImage
    string(REPLACE "${CMAKE_INSTALL_PREFIX}" ".${CMAKE_INSTALL_PREFIX}" SUNSHINE_ASSETS_DIR_DEF ${SUNSHINE_ASSETS_DIR})
endif()

# GIO
pkg_check_modules(GIO gio-2.0 gio-unix-2.0 REQUIRED)
if(GIO_FOUND)
    include_directories(SYSTEM ${GIO_INCLUDE_DIRS})
    list(APPEND PLATFORM_LIBRARIES ${GIO_LIBRARIES})
endif()

# AppImage and Flatpak
if (${SUNSHINE_BUILD_APPIMAGE})
    list(APPEND SUNSHINE_DEFINITIONS SUNSHINE_BUILD_APPIMAGE=1)
endif ()
if (${SUNSHINE_BUILD_FLATPAK})
    list(APPEND SUNSHINE_DEFINITIONS SUNSHINE_BUILD_FLATPAK=1)
endif ()

include_directories(SYSTEM
        ${CMAKE_BINARY_DIR}/generated-src)

list(APPEND PLATFORM_TARGET_FILES
        "${CMAKE_SOURCE_DIR}/src/platform/linux/publish.cpp"
        "${CMAKE_SOURCE_DIR}/src/platform/linux/misc.h"
        "${CMAKE_SOURCE_DIR}/src/platform/linux/misc.cpp")

list(APPEND PLATFORM_LIBRARIES dl)
