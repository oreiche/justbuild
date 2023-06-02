#!/bin/bash

set -euo pipefail

readonly ARCH="$(uname -m)"
readonly PREFIX="${PREFIX:-${HOME}/.local}"

function assert_tool() {
  if [ -z "$(which $1)" ]; then echo "Cannot find '$1' in PATH."; exit 1; fi
}

assert_tool curl
assert_tool jq
assert_tool wget
assert_tool tar

readonly URL=$(curl -s https://api.github.com/repos/oreiche/justbuild/releases \
               | jq -r '.[0].assets[].browser_download_url' \
               | grep "${ARCH}-linux\.tar\.gz$")

if [ -n "${URL}" ]; then
  echo "Installing JustBuild to '${PREFIX}'"
  mkdir -p "${PREFIX}"
  wget -nv "${URL}" -O - | tar xz --strip-components=1 -C "${PREFIX}"
else
  echo "Could not find latest (pre-)release for architecture '${ARCH}'."
  exit 1
fi
