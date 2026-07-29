cmake_minimum_required(VERSION 3.28)

project(sioxx_subproject_consumer LANGUAGES CXX)

include(CTest)
set(SIOXX_BUILD_TESTS ON CACHE BOOL "Build sioxx tests from the parent project")

add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../.." sioxx)

if(SIOXX_INSTALL)
  message(FATAL_ERROR "SIOXX_INSTALL must default to OFF when sioxx is a subproject")
endif()

add_executable(sioxx_subproject_consumer
  "${CMAKE_CURRENT_LIST_DIR}/../consumer/main.cpp"
)
target_link_libraries(sioxx_subproject_consumer PRIVATE sioxx::sioxx)

add_test(NAME sioxx_subproject_consumer COMMAND sioxx_subproject_consumer)
