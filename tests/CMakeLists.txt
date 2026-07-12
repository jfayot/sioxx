# include(FetchContent)
# FetchContent_Declare(
#     googletest
#     GIT_REPOSITORY https://github.com/google/googletest.git
#     GIT_TAG v1.15.2
#     GIT_SHALLOW TRUE
# )
CPMAddPackage(
  NAME googletest
  GITHUB_REPOSITORY google/googletest
  VERSION 1.15.2
  OPTIONS
  "INSTALL_GTEST OFF"
  "BUILD_GMOCK OFF"
  "gtest_force_shared_crt ON"
)

# Match sioxx's own runtime settings so gtest links cleanly alongside it.
# set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
# set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
# set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
# FetchContent_MakeAvailable(googletest)

add_executable(sioxx_tests
    test_message.cpp
    test_url_parse.cpp
    test_json_parser.cpp
    test_msgpack_parser.cpp
    test_socketio_socket.cpp
    test_engineio_client.cpp
)

target_link_libraries(sioxx_tests
    PRIVATE
        sioxx::sioxx
        # GTest::gtest
        GTest::gtest_main
)

target_include_directories(sioxx_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

include(GoogleTest)
gtest_discover_tests(sioxx_tests)
