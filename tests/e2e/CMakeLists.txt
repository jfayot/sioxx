find_program(SIOXX_NODE_EXECUTABLE node REQUIRED)

add_executable(sioxx_e2e_tests
  test_client.cpp
)

target_link_libraries(sioxx_e2e_tests
  PRIVATE
  sioxx::sioxx
  GTest::gtest_main
)

target_compile_features(sioxx_e2e_tests PRIVATE cxx_std_17)

add_test(
  NAME sioxx_e2e
  COMMAND
    "${SIOXX_NODE_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/server/run-tests.js"
    "$<TARGET_FILE:sioxx_e2e_tests>"
)

set_tests_properties(sioxx_e2e PROPERTIES
  LABELS "e2e"
  TIMEOUT 30
)
