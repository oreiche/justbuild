#/bin/bash

set -euo pipefail

if [ -z "${1:-}" ]; then
  echo "usage: $0 <debian-tag-on-salsa> [verify]"
  exit 1
fi

readonly REV=$1
readonly REPODIR=$(dirname $0)/..
readonly WORKDIR=$(mktemp -d)

function get_size() {
  cat "$1" | sed -n 's/\s*static const std::size_t\s'$2'\s=\s\([0-9]\+\);$/\1/p'
}

function get_string() {
  cat "$1" | sed -n 's/\s*std::string\s'$2'\s=.*\"\(.*\)\".*;$/\1/p'
}

function get_version() {
  local MAJOR=$(get_size $1 kMajor)
  local MINOR=$(get_size $1 kMinor)
  local PATCH=$(get_size $1 kRevision)
  local SUFFIX=$(get_string $1 suffix)

  local VERSION="$MAJOR.$MINOR.$PATCH"
  if [ -n "$SUFFIX" ]; then
    local COMMIT_TIME=$(git log -n1 --format=%ct ${REV})
    VERSION="${VERSION}${SUFFIX}+${COMMIT_TIME}"
  fi
  echo $VERSION
}

echo "Working in ${WORKDIR}"

( cd ${REPODIR}

  git show ${REV}:src/buildtool/main/version.cpp > ${WORKDIR}/.version.cpp
  readonly VERSION=$(get_version ${WORKDIR}/.version.cpp)
  readonly PKGDIR=${WORKDIR}/justbuild-${VERSION}
  readonly PKGTAR=${WORKDIR}/justbuild_${VERSION}.orig.tar

  mkdir ${PKGDIR}
  git archive -o ${PKGTAR} ${REV} ':!debian'
  git archive ${REV} | tar -xf - -C ${PKGDIR}
  xz ${PKGTAR}

  cd ${WORKDIR}

  echo
  echo "Build source package"
  docker run -v $(pwd):/data -u $(id -u):$(id -g) just-make-deb:debian_sid \
    /bin/bash -c "cd /data/justbuild-${VERSION}; dpkg-buildpackage -S"

  if [ -n "${2:-}" ]; then
    echo
    echo "Build binary package"
    docker run -v $(pwd):/data -u $(id -u):$(id -g) just-make-deb:debian_sid \
      /bin/bash -c "cd /data/justbuild-${VERSION}; dpkg-buildpackage -b"
  fi

  echo
  echo "Signing package"
  debsign -k 9DBDF6B510DD90EB justbuild_*_source.changes

  echo
  echo "Run following commands to build package"
  echo "  cd ${WORKDIR}"
  echo "  sudo pbuilder create --distribution sid --mirror http://ftp.us.debian.org/debian/"
  echo "  sudo pbuilder build justbuild_*.dsc"
  echo "Results will be staged to /var/cache/pbuilder/result/"

  echo
  echo "Run following commands to upload package"
  echo "  cd ${WORKDIR}"
  echo "  dput -f mentors justbuild_*_source.changes"
)

