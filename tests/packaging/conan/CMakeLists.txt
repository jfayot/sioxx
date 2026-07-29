cmake_minimum_required(VERSION 3.28)

project(sioxx_package_test LANGUAGES CXX)

find_package(sioxx CONFIG REQUIRED)

add_executable(sioxx_package_test src/main.cpp)
target_link_libraries(sioxx_package_test PRIVATE sioxx::sioxx)
target_compile_features(sioxx_package_test PRIVATE cxx_std_17)
