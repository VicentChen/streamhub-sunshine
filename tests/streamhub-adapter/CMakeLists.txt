cmake_minimum_required(VERSION 3.24)
project(streamhub-adapter-tests LANGUAGES CXX)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT DEFINED ENV{STREAMHUB_ROOT})
    message(FATAL_ERROR "Use the Linux product tools/run.py --development entrypoint")
endif()
file(REAL_PATH "$ENV{STREAMHUB_ROOT}" PRODUCT_ROOT)
file(REAL_PATH "${CMAKE_BINARY_DIR}" BUILD_ROOT)
cmake_path(IS_PREFIX PRODUCT_ROOT "${BUILD_ROOT}" NORMALIZE BUILD_INSIDE)
if(NOT BUILD_INSIDE OR BUILD_ROOT STREQUAL PRODUCT_ROOT)
    message(FATAL_ERROR "Build output must be below the product root")
endif()
get_filename_component(SUNSHINE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
find_package(Threads REQUIRED)
set(STREAMHUB_PROTOCOL_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${PRODUCT_ROOT}/protocol" protocol)
add_library(sunshine-streamhub STATIC
    "${SUNSHINE_ROOT}/src/streamhub/transport.cpp"
    "${SUNSHINE_ROOT}/src/streamhub/negotiation.cpp"
    "${SUNSHINE_ROOT}/src/streamhub/catalog.cpp"
    "${SUNSHINE_ROOT}/src/streamhub/receiver.cpp"
    "${SUNSHINE_ROOT}/src/streamhub/media.cpp"
    "${SUNSHINE_ROOT}/src/streamhub/gamepad.cpp")
target_link_libraries(sunshine-streamhub PUBLIC streamhub-protocal::protocol Threads::Threads)
target_include_directories(sunshine-streamhub PUBLIC "${SUNSHINE_ROOT}")
target_compile_options(sunshine-streamhub PRIVATE -Wall -Wextra -Wpedantic -Werror)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(INSTALL_GMOCK OFF CACHE BOOL "" FORCE)
add_subdirectory("${SUNSHINE_ROOT}/third-party/lizardbyte-common/third-party/googletest" googletest)
add_executable(test_streamhub_transport
    "../unit/test_streamhub_transport.cpp"
    "../unit/test-streamhub-session.cpp"
    "../unit/test-streamhub-media.cpp")
target_link_libraries(test_streamhub_transport PRIVATE sunshine-streamhub gtest_main)
add_executable(streamhub-gamepad-smoke "../streamhub-gamepad-smoke.cpp")
target_link_libraries(streamhub-gamepad-smoke PRIVATE sunshine-streamhub)
enable_testing()
add_test(NAME streamhub-adapter COMMAND test_streamhub_transport)
set_tests_properties(streamhub-adapter PROPERTIES TIMEOUT 30)
