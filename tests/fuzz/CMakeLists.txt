if(NOT CMAKE_CXX_COMPILER_ID MATCHES "^(Apple)?Clang$" OR MSVC)
  message(FATAL_ERROR "SIOXX_BUILD_FUZZ_TESTS requires Clang with libFuzzer")
endif()

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/artifacts")

foreach(parser IN ITEMS json msgpack)
  set(target sioxx_fuzz_${parser})
  set(corpus "${CMAKE_CURRENT_BINARY_DIR}/${parser}-corpus")
  file(MAKE_DIRECTORY "${corpus}")

  add_executable(${target} fuzz_${parser}.cpp "${PROJECT_SOURCE_DIR}/src/${parser}_parser.cpp")
  target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/include"
                                               "${PROJECT_SOURCE_DIR}/src")
  target_link_libraries(${target} PRIVATE nlohmann_json::nlohmann_json)
  target_compile_features(${target} PRIVATE cxx_std_17)
  target_compile_options(${target} PRIVATE -fsanitize=fuzzer,address,undefined
                                            -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
  target_link_options(${target} PRIVATE -fsanitize=fuzzer,address,undefined)

  add_test(
    NAME ${target}
    COMMAND ${target} "${corpus}" "${CMAKE_CURRENT_SOURCE_DIR}/corpus/${parser}"
            -max_total_time=10 -max_len=4096 -timeout=3 -rss_limit_mb=1024
            "-artifact_prefix=${CMAKE_CURRENT_BINARY_DIR}/artifacts/${parser}-"
  )
  set_tests_properties(${target} PROPERTIES LABELS fuzz TIMEOUT 30)
endforeach()
