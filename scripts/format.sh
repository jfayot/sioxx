#!/usr/bin/env bash

set -euo pipefail

if ! command -v clang-format >/dev/null 2>&1; then
  echo "clang-format is required but was not found in PATH" >&2
  exit 1
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"

cd "$repo_root"

files=()
while IFS= read -r -d '' file; do
  files+=("$file")
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

if [[ ${#files[@]} -eq 0 ]]; then
  echo "No tracked C or C++ files found."
  exit 0
fi

clang-format -i --style=file "${files[@]}"
echo "Formatted ${#files[@]} files."
