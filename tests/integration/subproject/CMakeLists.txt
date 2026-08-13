cmake_minimum_required(VERSION 3.28)

project(sioxx_subproject_consumer LANGUAGES CXX)

include(CTest)
include(FetchContent)

set(SIOXX_BUILD_TESTS
    ON
    CACHE BOOL "Build sioxx tests from the parent project"
)

FetchContent_Declare(
  Boost
  URL https://archives.boost.org/release/1.74.0/source/boost_1_74_0.tar.bz2
  URL_HASH SHA256=83bfc1507731a0906e387fc28b7ef5417d591429e51e788417fe9ff025e116b1
  SOURCE_SUBDIR __sioxx_no_cmake_subdir
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_Declare(
  nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.8.0/include.zip
  URL_HASH SHA256=8590fbcc2346a3eefc341935765dd57598022ada1081b425678f0da9a939a3c0
  SOURCE_SUBDIR __sioxx_no_cmake_subdir
  EXCLUDE_FROM_ALL
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_Declare(sioxx SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../..")
FetchContent_MakeAvailable(sioxx)

if(SIOXX_INSTALL)
  message(FATAL_ERROR "SIOXX_INSTALL must default to OFF when sioxx is a subproject")
endif()

add_executable(sioxx_subproject_consumer "${CMAKE_CURRENT_LIST_DIR}/../consumer/main.cpp")
target_link_libraries(sioxx_subproject_consumer PRIVATE sioxx::sioxx)

add_test(NAME sioxx_subproject_consumer COMMAND sioxx_subproject_consumer)
