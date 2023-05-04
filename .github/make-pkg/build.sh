#!/bin/bash

set -euo pipefail

# settings
readonly REF="${1:-HEAD}"
readonly PLF="${2:-ubuntu22.04}"
readonly PKG="${3:-deb}"
readonly NAME="justbuild"

# paths
readonly ROOTDIR=$(realpath $(dirname $0))
readonly WORKDIR=$(pwd)/work_${PLF}/source

# clean workdir
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
RELEASE=$(jq -r '."'${PLF}'"."distrelease" // ""' ${ROOTDIR}/platforms.json)
if [ -n "${RELEASE}" ]; then
  VERSION="$VERSION~${RELEASE}1"
fi

# rename source tree
mv ${SRCDIR} ${SRCDIR}-${VERSION}

# setup and build package from source tree
( cd ${SRCDIR}-${VERSION}

  # generate file system structure
  if [ "${PKG}" = "deb" ]; then
    export EMAIL="oliver.reiche@gmail.com"
    export DEBFULLNAME="Oliver Reiche"
    export DATADIR="$(pwd)/debian"
    dh_make -s -y --createorig
  elif [ "${PKG}" = "rpm" ]; then
    export HOME="${WORKDIR}"
    export DATADIR="$(pwd)/rpmbuild"
    rpmdev-setuptree
  else
    echo "Unknown pkg type '${PKG}'"
    exit 1
  fi

  # fetch distfiles of non-local deps
  NON_LOCAL_DEPS=$(jq -r '."'${PLF}'"."non-local-deps" // [] | tostring' ${ROOTDIR}/platforms.json)
  DISTFILES=${DATADIR}/distfiles
  mkdir -p ${DISTFILES}
  while read DEP; do
    if [ -z "${DEP}" ]; then continue; fi
    URL="$(jq -r '.repositories."'${DEP}'".repository.fetch' etc/repos.json)"
    wget -nv -P ${DISTFILES} "${URL}"
  done <<< $(echo ${NON_LOCAL_DEPS} | jq -r '.[]')

  # generate missing includes
  INCLUDE_PATH=${DATADIR}/include
  mkdir -p ${INCLUDE_PATH}
  while read HDR_FILE; do
    if [ -z "${HDR_FILE}" ]; then continue; fi
    HDR_DIR="$(dirname "${HDR_FILE}")"
    HDR_DATA="$(jq -r '."'${PLF}'"."gen-includes"."'${HDR_FILE}'" | tostring' \
                ${ROOTDIR}/platforms.json)"
    mkdir -p "${INCLUDE_PATH}/${HDR_DIR}"
    echo "${HDR_DATA}" > "${INCLUDE_PATH}/${HDR_FILE}"
  done <<< $(jq -r '."'${PLF}'"."gen-includes" // {} | keys | .[] | tostring' ${ROOTDIR}/platforms.json)

  # generate missing pkg-config files
  PKGCONFIG=${DATADIR}/pkgconfig
  mkdir -p ${PKGCONFIG}
  REQ_PC_FIELDS='{"Name":"","Version":"n/a","Description":"n/a","URL":"n/a"}'
  while read PKG_DESC; do
    if [ -z "${PKG_DESC}" ]; then continue; fi
    PKG_NAME=$(echo ${PKG_DESC} | jq -r '.Name')
    echo 'gen_includes="GEN_INCLUDES"' > ${PKGCONFIG}/${PKG_NAME}.pc
    echo ${REQ_PC_FIELDS} ${PKG_DESC} \
      | jq -sr 'add | keys_unsorted[] as $k | "\($k): \(.[$k])"' \
      >> ${PKGCONFIG}/${PKG_NAME}.pc
  done <<< $(jq -r '."'${PLF}'"."gen-pkgconfig" // [] | .[] | tostring' ${ROOTDIR}/platforms.json)

  echo ${NON_LOCAL_DEPS} > ${DATADIR}/non_local_deps
  echo ${SOURCE_DATE_EPOCH} > ${DATADIR}/source_date_epoch

  BUILD_DEPENDS=$(jq -r '."'${PLF}'"."build-depends" // [] | join(",")' ${ROOTDIR}/platforms.json)

  if [ "${PKG}" = "deb" ]; then
    # copy prepared debian files
    cp ${ROOTDIR}/debian/* ./debian/
    echo 12 > ./debian/compat
    mkdir -p ./debian/source
    find debian/distfiles -type f > ./debian/source/include-binaries

    # remove usused debian files
    rm -f ./debian/README.Debian

    # patch control file
    sed -i 's/BUILD_DEPENDS/'${BUILD_DEPENDS}'/' ./debian/control

    # build source package
    dpkg-buildpackage -S

    # patch changes file
    if [ -n "${RELEASE}" ]; then
      sed -i 's/UNRELEASED/'${RELEASE}'/' ${WORKDIR}/${NAME}_*_source.changes
    fi

    # build binary package
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

    # build source rpm
    rpmbuild -bs ${HOME}/rpmbuild/SPECS/justbuild.spec

    # build binary rpm from source rpm
    rpmbuild --rebuild ${HOME}/rpmbuild/SRPMS/justbuild-${VERSION}*.src.rpm
  fi
)
