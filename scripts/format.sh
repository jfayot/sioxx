#!/usr/bin/env bash

set -euo pipefail

if ! command -v clang-format >/dev/null 2>&1; then
  echo "clang-format is required but was not found in PATH" >&2
  exit 1
fi

if ! command -v cmake-format >/dev/null 2>&1; then
  echo "cmake-format is required but was not found in PATH" >&2
  exit 1
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"

cd "$repo_root"

cxx_files=()
while IFS= read -r -d '' file; do
  cxx_files+=("$file")
done < <(
  git ls-files -z -- \
    '*.c' \
    '*.cc' \
    '*.cpp' \
    '*.cxx' \
    '*.h' \
    '*.hh' \
    '*.hpp' \
    '*.hxx'
)

if [[ ${#cxx_files[@]} -eq 0 ]]; then
  echo "No tracked C or C++ files found."
else
  clang-format -i --style=file "${cxx_files[@]}"
  echo "Formatted ${#cxx_files[@]} C and C++ files."
fi

cmake_files=()
while IFS= read -r -d '' file; do
  cmake_files+=("$file")
done < <(
  git ls-files -z -- \
    ':(glob)**/CMakeLists.txt' \
    '*.cmake' \
    '*.cmake.in'
)

if [[ ${#cmake_files[@]} -eq 0 ]]; then
  echo "No tracked CMake files found."
else
  cmake-format -i --config-file "$repo_root/.cmake-format" "${cmake_files[@]}"
  echo "Formatted ${#cmake_files[@]} CMake files."
fi
