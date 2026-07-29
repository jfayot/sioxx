#!/usr/bin/env bash

set -euo pipefail

usage() {
  echo "Usage: $0 <version>" >&2
  echo "Example: $0 0.1.1" >&2
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
conan_file="$repo_root/conanfile.py"
readme_file="$repo_root/README.md"
docs_versions_file="$repo_root/docs/_static/versions.json"
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
if ! awk '
  $0 == "## [Unreleased]" { unreleased = 1; next }
  unreleased && /^## \[/ { exit }
  unreleased && /^- / { changes = 1 }
  END { exit !changes }
' "$changelog_file"; then
  echo "CHANGELOG.md has no release notes under [Unreleased]" >&2
  exit 1
fi
if ! grep -Fxq "    version = \"$current_version\"" "$conan_file"; then
  echo "Unexpected Conan recipe version; expected $current_version" >&2
  exit 1
fi
if ! grep -Fq "sioxx/$current_version" "$readme_file"; then
  echo "README.md has no sioxx/$current_version package reference" >&2
  exit 1
fi
if ! grep -Fq "\"name\": \"$current_version (current)\"" "$docs_versions_file" ||
  ! grep -Fq "\"version\": \"$current_version\"" "$docs_versions_file"; then
  echo "Unexpected current version in docs/_static/versions.json" >&2
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
conan_tmp="$(mktemp "$repo_root/.conanfile.py.XXXXXX")"
readme_tmp="$(mktemp "$repo_root/.README.md.XXXXXX")"
docs_versions_tmp="$(mktemp "$repo_root/.versions.json.XXXXXX")"
cleanup() {
  rm -f \
    "$cmake_tmp" \
    "$changelog_tmp" \
    "$conan_tmp" \
    "$readme_tmp" \
    "$docs_versions_tmp"
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

awk -v version="$version" -v old_version="$current_version" '
  !updated && $0 == "    version = \"" old_version "\"" {
    print "    version = \"" version "\""
    updated = 1
    next
  }
  { print }
  END { if (!updated) exit 1 }
' "$conan_file" > "$conan_tmp"

awk -v version="$version" -v old_version="$current_version" '
  {
    replaced += gsub("sioxx/" old_version, "sioxx/" version)
    print
  }
  END { if (!replaced) exit 1 }
' "$readme_file" > "$readme_tmp"

awk -v version="$version" -v old_version="$current_version" '
  {
    if ($0 == "    \"name\": \"" old_version " (current)\",") {
      print "    \"name\": \"" version " (current)\","
      name_updated = 1
    } else if ($0 == "    \"version\": \"" old_version "\",") {
      print "    \"version\": \"" version "\","
      version_updated = 1
    } else {
      print
    }
  }
  END { if (!name_updated || !version_updated) exit 1 }
' "$docs_versions_file" > "$docs_versions_tmp"

cp "$cmake_tmp" "$cmake_file"
cp "$changelog_tmp" "$changelog_file"
cp "$conan_tmp" "$conan_file"
cp "$readme_tmp" "$readme_file"
cp "$docs_versions_tmp" "$docs_versions_file"
cleanup
trap - EXIT

echo "Prepared sioxx v$version ($release_date)."
echo "Review the version and changelog changes, then commit and tag the release."
