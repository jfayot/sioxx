CPMGetPackage(googletest)

add_executable(sioxx_tests
  test_message.cpp
  test_url_parse.cpp
  test_json_parser.cpp
  test_msgpack_parser.cpp
  test_custom_parser.cpp
  test_socket.cpp
  test_engineio_client.cpp
  test_reconnection.cpp
)

target_link_libraries(sioxx_tests
  PRIVATE
  sioxx::sioxx
  GTest::gtest_main
)

target_include_directories(sioxx_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/src
)

include(GoogleTest)
enable_testing()
gtest_discover_tests(sioxx_tests)
