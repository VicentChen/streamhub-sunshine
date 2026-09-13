# linux specific packaging

install(DIRECTORY "${SUNSHINE_SOURCE_ASSETS_DIR}/linux/assets/"
        DESTINATION "${SUNSHINE_ASSETS_DIR}")

# copy assets (excluding shaders) to build directory, for running without install
file(COPY "${SUNSHINE_SOURCE_ASSETS_DIR}/linux/assets/"
        DESTINATION "${CMAKE_BINARY_DIR}/assets"
        PATTERN "shaders" EXCLUDE)
if(${SUNSHINE_BUILD_APPIMAGE} OR ${SUNSHINE_BUILD_FLATPAK})
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/app-${PROJECT_FQDN}.service"
            DESTINATION "${SUNSHINE_ASSETS_DIR}/systemd/user")
elseif(${SUNSHINE_BUILD_HOMEBREW})
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/app-${PROJECT_FQDN}.service"
            DESTINATION ".")
else()
    find_package(Systemd)

    if(SYSTEMD_FOUND)
        install(FILES "${CMAKE_CURRENT_BINARY_DIR}/app-${PROJECT_FQDN}.service"
                DESTINATION "${SYSTEMD_USER_UNIT_INSTALL_DIR}")
        endif()
endif()

# RPM specific
set(CPACK_RPM_PACKAGE_LICENSE "GPLv3")

# DEB specific
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
if(DEFINED ENV{DEBIAN_PACKAGE_RELEASE})  # cmake-lint: disable=W0106
    set(CPACK_DEBIAN_PACKAGE_RELEASE "$ENV{DEBIAN_PACKAGE_RELEASE}")
endif()

# FreeBSD specific
set(CPACK_FREEBSD_PACKAGE_MAINTAINER "${CPACK_PACKAGE_VENDOR}")
set(CPACK_FREEBSD_PACKAGE_ORIGIN "misc/${CPACK_PACKAGE_NAME}")
set(CPACK_FREEBSD_PACKAGE_LICENSE "GPLv3")

# Post install
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "${SUNSHINE_SOURCE_ASSETS_DIR}/linux/misc/postinst")
set(CPACK_RPM_POST_INSTALL_SCRIPT_FILE "${SUNSHINE_SOURCE_ASSETS_DIR}/linux/misc/postinst")


# Apply setcap for RPM
# https://github.com/coreos/rpm-ostree/discussions/5036#discussioncomment-10291071
set(CPACK_RPM_USER_FILELIST "%caps(cap_sys_nice+p) ${SUNSHINE_EXECUTABLE_PATH}")

# Dependencies
set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "\
            ${CPACK_DEB_PLATFORM_PACKAGE_DEPENDS} \
            debianutils, \
            libcap2, \
            libcurl4, \
            libopus0, \
            miniupnpc, \
            openssl | libssl3")
set(CPACK_RPM_PACKAGE_REQUIRES "\
            ${CPACK_RPM_PLATFORM_PACKAGE_REQUIRES} \
            libcap >= 2.22, \
            libcurl >= 7.0, \
            libopusenc >= 0.2.1, \
            miniupnpc >= 2.2.4, \
            openssl >= 3.0.2, \
            which >= 2.21")
list(APPEND CPACK_FREEBSD_PACKAGE_DEPS
        audio/opus
        ftp/curl
        net/avahi
        net/miniupnpc
        security/openssl
)

if(NOT BOOST_USE_STATIC)
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "\
                ${CPACK_DEBIAN_PACKAGE_DEPENDS}, \
                libboost-filesystem${Boost_VERSION}, \
                libboost-locale${Boost_VERSION}, \
                libboost-log${Boost_VERSION}")
    set(CPACK_RPM_PACKAGE_REQUIRES "\
                ${CPACK_RPM_PACKAGE_REQUIRES}, \
                boost-filesystem >= ${Boost_VERSION}, \
                boost-locale >= ${Boost_VERSION}, \
                boost-log >= ${Boost_VERSION}")
    list(APPEND CPACK_FREEBSD_PACKAGE_DEPS
            devel/boost-libs
    )
endif()

# This should automatically figure out dependencies on packages
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_RPM_PACKAGE_AUTOREQ ON)

# application icon
install(FILES "${CMAKE_SOURCE_DIR}/sunshine.svg"
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps"
        RENAME "${PROJECT_FQDN}.svg")


# desktop file
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_FQDN}.desktop"
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications")
if(NOT ${SUNSHINE_BUILD_APPIMAGE} AND NOT ${SUNSHINE_BUILD_FLATPAK})
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_FQDN}.terminal.desktop"
            DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications")
endif()

# metadata file
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_FQDN}.metainfo.xml"
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/metainfo")
