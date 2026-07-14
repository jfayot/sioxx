CPMAddPackage(
  NAME googletest
  GITHUB_REPOSITORY google/googletest
  VERSION 1.15.2
  OPTIONS
  "INSTALL_GTEST OFF"
  "BUILD_GMOCK OFF"
  "gtest_force_shared_crt ON"
)

add_executable(sioxx_tests
  test_message.cpp
  test_url_parse.cpp
  test_json_parser.cpp
  test_msgpack_parser.cpp
  test_socketio_socket.cpp
  test_engineio_client.cpp
  test_reconnection.cpp
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
