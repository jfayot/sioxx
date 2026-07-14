cmake_minimum_required(VERSION 3.20)

project(sioxx
  VERSION 0.0.4
  DESCRIPTION "Modern C++ socket.io client (nlohmann/json + Boost.Beast, JSON/MessagePack parsers)"
  LANGUAGES CXX
)

include(CTest)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(SIOXX_BUILD_EXAMPLES "Build example programs" ON)
option(SIOXX_BUILD_TESTS "Build unit tests" ON)
option(SIOXX_USE_SYSTEM_JSON "Use system-installed nlohmann_json instead of fetching it" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# download CPM.cmake
file(
  DOWNLOAD
  https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.43.1/CPM.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake
  EXPECTED_HASH SHA256=1c40fc102ce9625d7de7eb14f541cab30cc3138dca627f0b0ec40293ce6c2934
)
include(${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------

CPMAddPackage(
  NAME Boost
  VERSION 1.90.0
  URL https://github.com/boostorg/boost/releases/download/boost-1.90.0/boost-1.90.0-cmake.tar.xz
  URL_HASH SHA256=aca59f889f0f32028ad88ba6764582b63c916ce5f77b31289ad19421a96c555f
  OPTIONS
  "BOOST_ENABLE_CMAKE ON"
  "BUILD_SHARED_LIBS OFF"
  "BOOST_SKIP_INSTALL_RULES OFF"
  "BOOST_INCLUDE_LIBRARIES asio\\\;beast"
)

# TLS support for wss://
find_package(OpenSSL REQUIRED)

find_package(Threads REQUIRED)

if(SIOXX_USE_SYSTEM_JSON)
  find_package(nlohmann_json 3.12 REQUIRED)
else()
  CPMAddPackage(
    NAME nlohmann_json
    GITHUB_REPOSITORY nlohmann/json
    VERSION 3.12.0
    OPTIONS "JSON_BuildTests OFF" "JSON_Install OFF"
  )
  set(SIOXX_VENDORED_JSON_TARGET nlohmann_json)
endif()

# ---------------------------------------------------------------------------
# Library target
# ---------------------------------------------------------------------------
add_library(sioxx
  src/url_parse.cpp
  src/json_parser.cpp
  src/msgpack_parser.cpp
  src/websocket_transport.cpp
  src/http_polling_transport.cpp
  src/engineio_client.cpp
  src/reconnection.cpp
  src/socketio_socket.cpp
  src/socketio_client.cpp
)
add_library(sioxx::sioxx ALIAS sioxx)

target_include_directories(sioxx
  PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

target_link_libraries(sioxx
  PUBLIC
  nlohmann_json::nlohmann_json
  Boost::asio
  Boost::beast
  OpenSSL::SSL
  OpenSSL::Crypto
  Threads::Threads
)

target_compile_definitions(sioxx
  PUBLIC
  BOOST_ASIO_NO_DEPRECATED
  PRIVATE
  BOOST_ALL_NO_LIB
)

if(WIN32)
  target_link_libraries(sioxx PUBLIC ws2_32 mswsock)
  target_compile_options(sioxx PRIVATE /bigobj)
endif()

target_compile_features(sioxx PUBLIC cxx_std_17)

set_target_properties(sioxx PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
  EXPORT_NAME sioxx
)

# ---------------------------------------------------------------------------
# Examples
# ---------------------------------------------------------------------------
if(SIOXX_BUILD_EXAMPLES)
  add_executable(sioxx_basic_client examples/basic_client.cpp)
  target_link_libraries(sioxx_basic_client PRIVATE sioxx::sioxx)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(SIOXX_BUILD_TESTS)
  add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Install / export (so downstream projects can do find_package(sioxx))
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS sioxx ${SIOXX_VENDORED_JSON_TARGET}
  EXPORT sioxxTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(SIOXX_VENDORED_JSON_TARGET)
  install(DIRECTORY ${nlohmann_json_SOURCE_DIR}/include/nlohmann DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()

install(DIRECTORY include/sioxx DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT sioxxTargets
  FILE sioxxTargets.cmake
  NAMESPACE sioxx::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/sioxx
)

configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/sioxxConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/sioxxConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/sioxx
)

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/sioxxConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/sioxxConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/sioxxConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/sioxx
)
