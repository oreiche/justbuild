#!/bin/bash

set -euo pipefail

# settings
readonly REF="${1:-HEAD}"
readonly PLF="${2:-ubuntu22.04}"
readonly PKG="${3:-deb}"
readonly NAME="justbuild"

# paths
readonly ROOTDIR=$(realpath $(dirname $0))
readonly WORKDIR=$(pwd)/work_${PLF}
rm -rf ${WORKDIR}

# obtain time stamp from git commit
readonly SOURCE_DATE_EPOCH=$(git log -n1 --format=%ct ${REF})

# obtain snapshot of sources
SRCDIR="${WORKDIR}/${NAME}"
mkdir -p "${SRCDIR}"
git archive ${REF} | tar -x -C "${SRCDIR}"

# obtain version
VERSION_FILE="${SRCDIR}/src/buildtool/main/version.cpp"
MAJOR=$(cat ${VERSION_FILE} | sed -n 's/\s*std::size_t\smajor\s=\s\([0-9]\+\);$/\1/p')
MINOR=$(cat ${VERSION_FILE} | sed -n 's/\s*std::size_t\sminor\s=\s\([0-9]\+\);$/\1/p')
PATCH=$(cat ${VERSION_FILE} | sed -n 's/\s*std::size_t\srevision\s=\s\([0-9]\+\);$/\1/p')
SUFFIX=$(cat ${VERSION_FILE} | sed -n 's/\s*std::string\ssuffix\s=\s\"\(.*\)\";$/\1/p')
VERSION="$MAJOR.$MINOR.$PATCH"
if [ -n "$SUFFIX" ]; then
  VERSION="$VERSION$SUFFIX"
  if [ -n "$SOURCE_DATE_EPOCH" ]; then
    VERSION="$VERSION+$SOURCE_DATE_EPOCH"
  fi
fi

# rename source tree
mv ${SRCDIR} ${SRCDIR}-${VERSION}

# setup and build package from source tree
( cd ${SRCDIR}-${VERSION}

  # generate file system structure
  if [ "${PKG}" = "deb" ]; then
    export EMAIL="oliver.reiche@gmail.com"
    export DEBFULLNAME="Oliver Reiche"
    export AUXDIR="$(pwd)/debian"
    dh_make -s -y --createorig
  elif [ "${PKG}" = "rpm" ]; then
    export HOME="${WORKDIR}"
    export AUXDIR="$(pwd)/rpmbuild"
    rpmdev-setuptree
  else
    echo "Unknown pkg type '${PKG}'"
    exit 1
  fi

  # fetch distfiles of non-local deps
  NON_LOCAL_DEPS=$(jq -r '."'${PLF}'"."non-local-deps" // [] | tostring' ${ROOTDIR}/platforms.json)
  DISTFILES=${AUXDIR}/distfiles
  mkdir -p ${DISTFILES}
  while read DEP; do
    if [ -z "${DEP}" ]; then continue; fi
    URL="$(jq -r '.repositories."'${DEP}'".repository.fetch' etc/repos.json)"
    wget -nv -P ${DISTFILES} "${URL}"
  done <<< $(echo ${NON_LOCAL_DEPS} | jq -r '.[]')

  # generate missing pkg-config files
  PKGCONFIG=${AUXDIR}/pkgconfig
  mkdir -p ${PKGCONFIG}
  REQ_PC_FIELDS='{"Name":"","Version":"n/a","Description":"n/a","URL":"n/a"}'
  while read PKG_DESC; do
    if [ -z "${PKG_DESC}" ]; then continue; fi
    PKG_NAME=$(echo ${PKG_DESC} | jq -r '.Name')
    echo ${REQ_PC_FIELDS} ${PKG_DESC} \
      | jq -sr 'add | keys_unsorted[] as $k | "\($k): \(.[$k])"' \
      > ${PKGCONFIG}/${PKG_NAME}.pc
  done <<< $(jq -r '."'${PLF}'"."gen-pkgconfig" // [] | .[] | tostring' ${ROOTDIR}/platforms.json)

  echo ${NON_LOCAL_DEPS} > ${AUXDIR}/non_local_deps
  echo ${SOURCE_DATE_EPOCH} > ${AUXDIR}/source_date_epoch

  BUILD_DEPENDS=$(jq -r '."'${PLF}'"."build-depends" // [] | join(",")' ${ROOTDIR}/platforms.json)

  if [ "${PKG}" = "deb" ]; then
    # copy prepared debian files
    cp ${ROOTDIR}/debian/* ./debian/
    echo 12 > ./debian/compat

    # remove usused debian files
    rm -f ./debian/README.Debian

    # patch control file
    sed -i 's/BUILD_DEPENDS/'${BUILD_DEPENDS}'/' ./debian/control

    # run build
    dpkg-buildpackage -b
  elif [ "${PKG}" = "rpm" ]; then
    # copy prepared rpmbuild files
    cp ${ROOTDIR}/rpmbuild/justbuild.makefile ./rpmbuild/
    cp ${ROOTDIR}/rpmbuild/justbuild.spec ${HOME}/rpmbuild/SPECS/

    # patch spec file
    sed -i 's/VERSION/'${VERSION}'/' ${HOME}/rpmbuild/SPECS/justbuild.spec
    sed -i 's/BUILD_DEPENDS/'${BUILD_DEPENDS}'/' ${HOME}/rpmbuild/SPECS/justbuild.spec

    # create archive
    cd ${WORKDIR}
    tar -czf ${HOME}/rpmbuild/SOURCES/${NAME}-${VERSION}.tar.gz ${NAME}-${VERSION}

    # run build
    rpmbuild -bb ${HOME}/rpmbuild/SPECS/justbuild.spec
  fi
)
