#!/usr/bin/env bash

set -euo pipefail

usage() {
  echo "Usage: $0 <version>" >&2
  echo "Example: $0 0.0.5" >&2
}

if [[ $# -ne 1 ]]; then
  usage
  exit 2
fi

version="${1#v}"
if [[ ! "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+([.-][0-9A-Za-z.-]+)?$ ]]; then
  echo "Invalid semantic version: $1" >&2
  exit 2
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
cmake_file="$repo_root/CMakeLists.txt"
changelog_file="$repo_root/CHANGELOG.md"
release_date="$(date +%Y-%m-%d)"

current_version="$(
  awk '
    /^[[:space:]]*VERSION[[:space:]]+[0-9]/ {
      print $2
      exit
    }
  ' "$cmake_file"
)"

if [[ -z "$current_version" ]]; then
  echo "Could not find the project version in CMakeLists.txt" >&2
  exit 1
fi
if [[ "$version" == "$current_version" ]]; then
  echo "CMakeLists.txt is already at version $version" >&2
  exit 1
fi
if grep -Fq "## [$version]" "$changelog_file"; then
  echo "CHANGELOG.md already contains version $version" >&2
  exit 1
fi
if ! grep -Fq "## [Unreleased]" "$changelog_file"; then
  echo "CHANGELOG.md has no [Unreleased] section" >&2
  exit 1
fi

expected_unreleased="[Unreleased]: https://github.com/jfayot/sioxx/compare/v${current_version}...HEAD"
if ! grep -Fxq "$expected_unreleased" "$changelog_file"; then
  echo "Unexpected [Unreleased] comparison link; expected:" >&2
  echo "  $expected_unreleased" >&2
  exit 1
fi

cmake_tmp="$(mktemp "$repo_root/.CMakeLists.txt.XXXXXX")"
changelog_tmp="$(mktemp "$repo_root/.CHANGELOG.md.XXXXXX")"
cleanup() {
  rm -f "$cmake_tmp" "$changelog_tmp"
}
trap cleanup EXIT

awk -v version="$version" '
  !updated && /^[[:space:]]*VERSION[[:space:]]+[0-9]/ {
    sub(/[0-9]+\.[0-9]+\.[0-9]+([^[:space:]]*)?/, version)
    updated = 1
  }
  { print }
  END { if (!updated) exit 1 }
' "$cmake_file" > "$cmake_tmp"

awk \
  -v version="$version" \
  -v old_version="$current_version" \
  -v release_date="$release_date" '
  $0 == "## [Unreleased]" {
    print
    print ""
    print "## [" version "] - " release_date
    released = 1
    next
  }
  $0 == "[Unreleased]: https://github.com/jfayot/sioxx/compare/v" old_version "...HEAD" {
    print "[Unreleased]: https://github.com/jfayot/sioxx/compare/v" version "...HEAD"
    print "[" version "]: https://github.com/jfayot/sioxx/compare/v" old_version "...v" version
    linked = 1
    next
  }
  { print }
  END { if (!released || !linked) exit 1 }
' "$changelog_file" > "$changelog_tmp"

cp "$cmake_tmp" "$cmake_file"
cp "$changelog_tmp" "$changelog_file"
cleanup
trap - EXIT

echo "Prepared sioxx v$version ($release_date)."
echo "Review CMakeLists.txt and CHANGELOG.md, then commit and tag the release."
