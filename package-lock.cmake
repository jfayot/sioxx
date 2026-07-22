# CPM Package Lock
# This file should be committed to version control

# Boost
CPMDeclarePackage(Boost
  NAME Boost
  VERSION 1.90.0
  URL https://github.com/boostorg/boost/releases/download/boost-1.90.0/boost-1.90.0-cmake.tar.xz
  URL_HASH SHA256=aca59f889f0f32028ad88ba6764582b63c916ce5f77b31289ad19421a96c555f
  OPTIONS
  "BOOST_ENABLE_CMAKE ON"
  "BUILD_SHARED_LIBS OFF"
  "BOOST_SKIP_INSTALL_RULES OFF"
  EXCLUDE_FROM_ALL True
)

# nlohmann_json
CPMDeclarePackage(nlohmann_json
  NAME nlohmann_json
  GITHUB_REPOSITORY nlohmann/json
  VERSION 3.12.0
  OPTIONS
  "JSON_BuildTests OFF"
  "JSON_Install OFF"
  "JSON_MultipleHeaders OFF"
  "JSON_ImplicitConversions OFF"
)

# googletest
CPMDeclarePackage(googletest
  NAME googletest
  GITHUB_REPOSITORY google/googletest
  VERSION 1.15.2
  OPTIONS
  "INSTALL_GTEST OFF"
  "BUILD_GMOCK OFF"
  "gtest_force_shared_crt ON"
  OPTIONS
  EXCLUDE_FROM_ALL True
)
