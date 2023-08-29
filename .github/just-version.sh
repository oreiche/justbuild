#!/bin/bash

set -euo pipefail

readonly VERSION_FILE=$(cat ./src/buildtool/main/version.cpp)

function get_size() {
  echo "$VERSION_FILE" | sed -n 's/\s*std::size_t\s'$1'\s=\s\([0-9]\+\);$/\1/p'
}

function get_string() {
  echo "$VERSION_FILE" | sed -n 's/\s*std::string\s'$1'\s=.*\"\(.*\)\".*;$/\1/p'
}

readonly MAJOR=$(get_size major)
readonly MINOR=$(get_size minor)
readonly PATCH=$(get_size revision)
readonly SUFFIX=$(get_string suffix)

VERSION="$MAJOR.$MINOR.$PATCH"
if [ -n "$SUFFIX" ]; then
  readonly REF=${1:-HEAD}
  readonly COMMIT_TIME=$(git log -n1 --format=%ct ${REF})
  VERSION="${VERSION}${SUFFIX}+${COMMIT_TIME}"
fi

echo $VERSION
