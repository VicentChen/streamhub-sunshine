# macos specific compile definitions

add_compile_definitions(SUNSHINE_PLATFORM="macos")

if (SUNSHINE_BUILD_HOMEBREW)
    set(SUNSHINE_ASSETS_DIR "${CMAKE_INSTALL_PREFIX}/${SUNSHINE_ASSETS_DIR}")
else()
    # Bundle layout for macOS app builds
    set(SUNSHINE_ASSETS_DIR "${CMAKE_PROJECT_NAME}.app/Contents/Resources/assets")
    set(SUNSHINE_ASSETS_DIR_DEF "../Resources/assets")
endif()

set(MACOS_LINK_DIRECTORIES
        /opt/homebrew/lib
        /opt/local/lib
        /usr/local/lib)

foreach(dir ${MACOS_LINK_DIRECTORIES})
    if(EXISTS ${dir})
        link_directories(${dir})
    endif()
endforeach()

if(NOT BOOST_USE_STATIC AND NOT FETCH_CONTENT_BOOST_USED)
    ADD_DEFINITIONS(-DBOOST_LOG_DYN_LINK)
endif()

list(APPEND SUNSHINE_EXTERNAL_LIBRARIES
        ${FOUNDATION_LIBRARY})

set(APPLE_PLIST_TEMPLATE "${SUNSHINE_SOURCE_ASSETS_DIR}/macos/build/Info.plist.in")
set(APPLE_PLIST_FILE "${CMAKE_BINARY_DIR}/Info.plist")
set(APPLE_ENTITLEMENTS_FILE "${SUNSHINE_SOURCE_ASSETS_DIR}/macos/entitlements.plist")
configure_file("${APPLE_PLIST_TEMPLATE}" "${APPLE_PLIST_FILE}" @ONLY)

set(PLATFORM_TARGET_FILES
        "${CMAKE_SOURCE_DIR}/src/platform/macos/misc.mm"
        "${CMAKE_SOURCE_DIR}/src/platform/macos/publish.cpp"
        ${APPLE_ENTITLEMENTS_FILE}
        ${APPLE_PLIST_FILE})
