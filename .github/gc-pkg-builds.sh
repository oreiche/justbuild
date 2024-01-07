#!/bin/bash

set -euo pipefail

for i in $(echo work_*); do
  if [ -d $i/build/.just/protocol-dependent/generation-0 ]; then
    echo $i
    just gc --local-build-root $i/build/.just
  fi
done

