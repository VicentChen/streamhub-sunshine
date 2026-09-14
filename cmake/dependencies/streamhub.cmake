# Public MIT protocol dependency; no Provider sources or libraries.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(STREAMHUB_PROTOCOL_SOURCE_DIR "${CMAKE_SOURCE_DIR}/../protocol" CACHE PATH
        "Path to the streamhub-protocal public protocol checkout")
    if(NOT EXISTS "${STREAMHUB_PROTOCOL_SOURCE_DIR}/include/streamhub-protocal/protocol.hpp")
        message(FATAL_ERROR "StreamHub protocol missing: initialize the parent protocol submodule or set STREAMHUB_PROTOCOL_SOURCE_DIR")
    endif()
    include(GNUInstallDirs)
    install(FILES "${STREAMHUB_PROTOCOL_SOURCE_DIR}/LICENSE"
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/licenses/sunshine"
        RENAME streamhub-protocol-LICENSE)
    add_subdirectory("${STREAMHUB_PROTOCOL_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/streamhub-protocol")
    add_library(sunshine-streamhub STATIC
        "${CMAKE_SOURCE_DIR}/src/streamhub/transport.cpp"
        "${CMAKE_SOURCE_DIR}/src/streamhub/negotiation.cpp"
        "${CMAKE_SOURCE_DIR}/src/streamhub/catalog.cpp"
        "${CMAKE_SOURCE_DIR}/src/streamhub/receiver.cpp"
        "${CMAKE_SOURCE_DIR}/src/streamhub/media.cpp"
        "${CMAKE_SOURCE_DIR}/src/streamhub/gamepad.cpp")
    target_link_libraries(sunshine-streamhub PUBLIC streamhub-protocal::protocol Threads::Threads)
    target_include_directories(sunshine-streamhub PUBLIC "${CMAKE_SOURCE_DIR}/src")
    list(APPEND SUNSHINE_EXTERNAL_LIBRARIES sunshine-streamhub)
endif()
