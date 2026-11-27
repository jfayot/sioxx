cmake_minimum_required(VERSION 3.28)

project(sioxx_consumer LANGUAGES CXX)

# Release preparation requests the current 0.x minor or the stable major's .0.
find_package(sioxx 0.3 CONFIG REQUIRED)

option(SIOXX_USE_SYSTEM_BOOST "Use system Boost for the consumer's Asio context" ON)

if(SIOXX_USE_SYSTEM_BOOST)
  find_package(Boost 1.74.0 REQUIRED CONFIG)
  set(consumer_boost_target Boost::headers)
else()
  include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/SioxxBuildHelpers.cmake")
  sioxx_fetch_boost(consumer_boost_target)
endif()

add_executable(sioxx_consumer main.cpp)
target_link_libraries(sioxx_consumer PRIVATE sioxx::sioxx ${consumer_boost_target})

enable_testing()
add_test(NAME sioxx_consumer COMMAND sioxx_consumer)
