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

set(
  SIOXX_E2E_TEST_LAUNCHER
  "${SIOXX_NODE_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/server/run-tests.js"
)

if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.29)
  set_property(TARGET sioxx_e2e_tests PROPERTY TEST_LAUNCHER "${SIOXX_E2E_TEST_LAUNCHER}")
else()
  set_property(
    TARGET sioxx_e2e_tests PROPERTY CROSSCOMPILING_EMULATOR "${SIOXX_E2E_TEST_LAUNCHER}"
  )
endif()

include(GoogleTest)
gtest_discover_tests(
  sioxx_e2e_tests
  DISCOVERY_MODE PRE_TEST
  PROPERTIES LABELS "e2e" TIMEOUT 30
)
