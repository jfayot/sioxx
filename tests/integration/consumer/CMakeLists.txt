cmake_minimum_required(VERSION 3.28)

project(sioxx_consumer LANGUAGES CXX)

find_package(sioxx 0.2 CONFIG REQUIRED)

add_executable(sioxx_consumer main.cpp)
target_link_libraries(sioxx_consumer PRIVATE sioxx::sioxx)

enable_testing()
add_test(NAME sioxx_consumer COMMAND sioxx_consumer)
