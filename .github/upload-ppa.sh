#!/bin/bash

set -euo pipefail

if [ "${#@}" = 0 ]; then
  # upload all if not specified otherwise
  set -- $(echo work_ubuntu*)
fi

for i in $@; do
  echo $i
  pushd "$i/source"
  debsign -k FD141C1EFEF4D7D2 --re-sign justbuild_*_source.changes
  dput -f justbuild-ppa justbuild_*_source.changes
  popd
done
