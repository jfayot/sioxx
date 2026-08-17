set(INSTALL_GTEST OFF)
set(BUILD_GMOCK OFF)
set(gtest_force_shared_crt ON)

FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
  URL_HASH SHA256=7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(googletest)

add_executable(
  sioxx_tests
  test_message.cpp
  test_engineio_url.cpp
  test_url_parse.cpp
  test_json_parser.cpp
  test_msgpack_parser.cpp
  test_custom_parser.cpp
  test_socket.cpp
  test_engineio_client.cpp
  test_http_polling_transport.cpp
  test_reconnection.cpp
)

target_link_libraries(sioxx_tests PRIVATE sioxx::sioxx GTest::gtest_main)

target_include_directories(
  sioxx_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/src"
)

include(GoogleTest)
gtest_discover_tests(sioxx_tests DISCOVERY_MODE PRE_TEST)
