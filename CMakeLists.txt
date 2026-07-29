cmake_minimum_required(VERSION 3.28)

project(sioxx
  VERSION 0.1.0
  DESCRIPTION "A modern C++17 Socket.IO client built on Boost.Beast and nlohmann-json"
  LANGUAGES CXX
)

include(GNUInstallDirs)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(SIOXX_BUILD_EXAMPLES "Build example programs" ${PROJECT_IS_TOP_LEVEL})
option(SIOXX_BUILD_TESTS "Build unit tests" ${PROJECT_IS_TOP_LEVEL})
option(SIOXX_INSTALL "Generate sioxx installation rules" ${PROJECT_IS_TOP_LEVEL})
option(SIOXX_BUILD_DOCS "Build documentation" OFF)
option(SIOXX_USE_SYSTEM_BOOST "Use system-installed boost instead of fetching it" OFF)
option(SIOXX_USE_SYSTEM_JSON "Use system-installed nlohmann_json instead of fetching it" OFF)
option(SIOXX_ENABLE_CLANG_TIDY "Run clang-tidy while compiling sioxx targets" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(SioxxBuildHelpers)

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------

if(SIOXX_USE_SYSTEM_BOOST)
  find_package(Boost 1.90.0 REQUIRED CONFIG)
  set(SIOXX_BOOST_BUILD_TARGETS Boost::headers)
else()
  sioxx_fetch_boost(SIOXX_BOOST_BUILD_TARGETS)
endif()

if(SIOXX_USE_SYSTEM_JSON)
  find_package(nlohmann_json 3.12.0 REQUIRED)
else()
  sioxx_fetch_nlohmann_json(
    SIOXX_VENDORED_JSON_TARGET
    SIOXX_VENDORED_JSON_SOURCE_DIR
  )
endif()

# TLS support for wss://
find_package(OpenSSL REQUIRED)

find_package(Threads REQUIRED)

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
  src/socket.cpp
  src/client.cpp
)
add_library(sioxx::sioxx ALIAS sioxx)

if(SIOXX_ENABLE_CLANG_TIDY)
  sioxx_find_clang_tidy(SIOXX_CLANG_TIDY_COMMAND)
  set_target_properties(sioxx PROPERTIES CXX_CLANG_TIDY "${SIOXX_CLANG_TIDY_COMMAND}")
endif()

target_include_directories(sioxx
  PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
  PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(sioxx
  PUBLIC
  nlohmann_json::nlohmann_json
  "$<BUILD_INTERFACE:${SIOXX_BOOST_BUILD_TARGETS}>"
  PRIVATE
  OpenSSL::SSL
  OpenSSL::Crypto
  Threads::Threads
)

target_compile_definitions(sioxx
  PRIVATE
  BOOST_ASIO_NO_DEPRECATED
  BOOST_ALL_NO_LIB
)

if(WIN32)
  target_link_libraries(sioxx PRIVATE ws2_32 mswsock)
  set_target_properties(sioxx PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

if(MSVC)
  target_compile_options(sioxx PRIVATE /bigobj)
endif()

target_compile_features(sioxx PUBLIC cxx_std_17)

set_target_properties(sioxx PROPERTIES
  VERSION ${PROJECT_VERSION}
  #TODO change to "${PROJECT_VERSION_MAJOR}" when releasing 1.0.0
  SOVERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}"
  EXPORT_NAME sioxx
  CXX_EXTENSIONS OFF
  POSITION_INDEPENDENT_CODE ON
)

if(PROJECT_IS_TOP_LEVEL
  AND NOT CMAKE_BUILD_TYPE
  AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type")
endif()

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
  if(PROJECT_IS_TOP_LEVEL)
    set(BUILD_TESTING ON)
    include(CTest)
  elseif(NOT BUILD_TESTING)
    message(FATAL_ERROR
      "The parent project must enable CTest before enabling SIOXX_BUILD_TESTS"
    )
  endif()

  add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Documentation
# ---------------------------------------------------------------------------
if(SIOXX_BUILD_DOCS)
  add_subdirectory(docs)
endif()

# ---------------------------------------------------------------------------
# Install / export (so downstream projects can do find_package(sioxx))
# ---------------------------------------------------------------------------
if(SIOXX_INSTALL)
  include(CMakePackageConfigHelpers)

  get_target_property(SIOXX_LIBRARY_TYPE sioxx TYPE)
  if(SIOXX_LIBRARY_TYPE STREQUAL "STATIC_LIBRARY")
    set(SIOXX_FIND_PRIVATE_LINK_DEPENDENCIES ON)
  else()
    set(SIOXX_FIND_PRIVATE_LINK_DEPENDENCIES OFF)
  endif()

  install(TARGETS sioxx ${SIOXX_VENDORED_JSON_TARGET}
    EXPORT sioxxTargets
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  )

  if(SIOXX_VENDORED_JSON_TARGET)
    install(DIRECTORY "${SIOXX_VENDORED_JSON_SOURCE_DIR}/single_include/nlohmann"
      DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    )

    if(MSVC)
      install(FILES "${SIOXX_VENDORED_JSON_SOURCE_DIR}/nlohmann_json.natvis"
        DESTINATION "."
      )
    endif()
  endif()

  install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/sioxx"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
  )

  install(EXPORT sioxxTargets
    FILE sioxxTargets.cmake
    NAMESPACE sioxx::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/sioxx"
  )

  configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/sioxxConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/sioxxConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/sioxx"
  )

  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/sioxxConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    #TODO change to SameMajorVersion when releasing 1.0.0
    COMPATIBILITY SameMinorVersion
  )

  install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/sioxxConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/sioxxConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/sioxx"
  )
endif()
