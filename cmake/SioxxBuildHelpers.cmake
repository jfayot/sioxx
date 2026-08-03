include_guard(GLOBAL)

include(FetchContent)

function(sioxx_find_clang_tidy out_command)
  find_program(SIOXX_CLANG_TIDY_EXECUTABLE NAMES clang-tidy REQUIRED)
  set(${out_command}
    "${SIOXX_CLANG_TIDY_EXECUTABLE}"
    "--config-file=${PROJECT_SOURCE_DIR}/.clang-tidy"
    PARENT_SCOPE
  )
endfunction()

function(sioxx_fetch_boost out_target)
  FetchContent_Declare(Boost
    URL https://github.com/boostorg/boost/releases/download/boost-1.90.0/boost-1.90.0-cmake.tar.xz
    URL_HASH SHA256=aca59f889f0f32028ad88ba6764582b63c916ce5f77b31289ad19421a96c555f
    SOURCE_SUBDIR __sioxx_no_cmake_subdir
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )

  FetchContent_MakeAvailable(Boost)

  file(GLOB boost_include_dirs LIST_DIRECTORIES TRUE "${boost_SOURCE_DIR}/libs/*/include")

  if(NOT boost_include_dirs)
    message(FATAL_ERROR "Fetched Boost archive has an unsupported layout")
  endif()

  if(NOT TARGET sioxx_boost_headers)
    add_library(sioxx_boost_headers INTERFACE)
    target_include_directories(sioxx_boost_headers SYSTEM INTERFACE "${boost_include_dirs}")
  endif()

  set(${out_target} sioxx_boost_headers PARENT_SCOPE)
endfunction()

function(sioxx_fetch_nlohmann_json out_target out_source_dir)
  FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    EXCLUDE_FROM_ALL
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )

  FetchContent_MakeAvailable(nlohmann_json)

  set(${out_target} nlohmann_json PARENT_SCOPE)
  set(${out_source_dir} "${nlohmann_json_SOURCE_DIR}" PARENT_SCOPE)
endfunction()
