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

  if(EXISTS "${boost_SOURCE_DIR}/boost/version.hpp")
    set(boost_include_dirs "${boost_SOURCE_DIR}")
  else()
    file(GLOB boost_include_dirs LIST_DIRECTORIES TRUE "${boost_SOURCE_DIR}/libs/*/include")
  endif()

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
    SOURCE_SUBDIR __sioxx_no_cmake_subdir
    EXCLUDE_FROM_ALL
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )

  FetchContent_MakeAvailable(nlohmann_json)

  if(NOT TARGET nlohmann_json::nlohmann_json)
    if(EXISTS "${nlohmann_json_SOURCE_DIR}/single_include/nlohmann/json.hpp")
      set(nlohmann_json_include_dir "${nlohmann_json_SOURCE_DIR}/single_include")
    elseif(EXISTS "${nlohmann_json_SOURCE_DIR}/include/nlohmann/json.hpp")
      set(nlohmann_json_include_dir "${nlohmann_json_SOURCE_DIR}/include")
    else()
      message(FATAL_ERROR "Fetched nlohmann-json archive has an unsupported layout")
    endif()

    add_library(nlohmann_json INTERFACE)
    add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
    target_include_directories(
      nlohmann_json
      SYSTEM INTERFACE "$<BUILD_INTERFACE:${nlohmann_json_include_dir}>"
                       "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
    )
    target_compile_features(nlohmann_json INTERFACE cxx_std_11)

    if(MSVC AND EXISTS "${nlohmann_json_SOURCE_DIR}/nlohmann_json.natvis")
      target_sources(
        nlohmann_json
        INTERFACE "$<BUILD_INTERFACE:${nlohmann_json_SOURCE_DIR}/nlohmann_json.natvis>"
                  "$<INSTALL_INTERFACE:nlohmann_json.natvis>"
      )
    endif()
  endif()

  set(${out_target} nlohmann_json PARENT_SCOPE)
  set(${out_source_dir} "${nlohmann_json_SOURCE_DIR}" PARENT_SCOPE)
endfunction()
