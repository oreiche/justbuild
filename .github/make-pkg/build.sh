#!/bin/bash

set -euo pipefail

# settings
readonly REF="${1:-HEAD}"
readonly PLF="${2:-ubuntu22.04}"
readonly PKG="${3:-deb}"
readonly TARBALL="${4:-""}" # optional path to binary tarball
readonly NAME="justbuild"
readonly GH_ORG="just-buildsystem"

# paths
readonly CWD=$(pwd)
readonly ROOTDIR=$(realpath $(dirname $0))
readonly WORKDIR=${CWD}/work_${PLF}/source

# clean workdir
rm -rf ${WORKDIR}

# obtain time stamp from git commit
readonly COMMIT_HASH=$(git rev-parse ${REF})
readonly SOURCE_DATE_EPOCH=$(git log -n1 --format=%ct ${COMMIT_HASH})

# obtain snapshot of sources
SRCDIR="${WORKDIR}/${NAME}"
mkdir -p "${SRCDIR}"
git archive ${REF} | tar -x -C "${SRCDIR}"

# obtain version
VERSION_FILE="${SRCDIR}/src/buildtool/main/version.cpp"
MAJOR=$(cat ${VERSION_FILE} | sed -n 's/\s*static const std::size_t\skMajor\s=\s\([0-9]\+\);.*$/\1/p')
MINOR=$(cat ${VERSION_FILE} | sed -n 's/\s*static const std::size_t\skMinor\s=\s\([0-9]\+\);.*$/\1/p')
PATCH=$(cat ${VERSION_FILE} | sed -n 's/\s*static const std::size_t\skRevision\s=\s\([0-9]\+\);.*$/\1/p')
SUFFIX=$(cat ${VERSION_FILE} | sed -n 's/\s*std::string\ssuffix\s=.*\"\(.*\)\".*;.*$/\1/p')
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
    mkdir -p "${DATADIR}"
    rpmdev-setuptree
  elif [ "${PKG}" = "tar" ]; then
    export VERSION SOURCE_DATE_EPOCH
    export TARGET_ARCH=$(jq -r '."'${PLF}'"."target-arch" // "x86_64"' ${ROOTDIR}/platforms.json)
    ${ROOTDIR}/build-tarball.sh
    exit 0
  else
    echo "Unknown pkg type '${PKG}'"
    exit 1
  fi

  # fetch distfiles of non-local deps
  NON_LOCAL_DEPS=$(jq -r '."'${PLF}'"."non-local-deps" // [] | tostring' ${ROOTDIR}/platforms.json)
  DISTFILES=${DATADIR}/third_party
  INFOFILE=${DISTFILES}/info.txt
  rm -f ${INFOFILE}
  while read DEP; do
    if [ -z "${DEP}" ]; then continue; fi
    mkdir -p ${DISTFILES}
    URL="$(jq -r '.repositories."'${DEP}'".repository.fetch' etc/repos.json)"
    wget --no-check-certificate -nv -P ${DISTFILES} "${URL}"
    echo "$(basename "${URL}"): ${URL}" >> ${INFOFILE}
  done <<< $(echo ${NON_LOCAL_DEPS} | jq -r '.[]')

  # generate missing includes
  INCLUDE_PATH=${DATADIR}/include
  while read HDR_FILE; do
    if [ -z "${HDR_FILE}" ]; then continue; fi
    mkdir -p ${INCLUDE_PATH}
    HDR_DIR="$(dirname "${HDR_FILE}")"
    HDR_DATA="$(jq -r '."'${PLF}'"."gen-includes"."'${HDR_FILE}'" | tostring' \
                ${ROOTDIR}/platforms.json)"
    mkdir -p "${INCLUDE_PATH}/${HDR_DIR}"
    echo "${HDR_DATA}" > "${INCLUDE_PATH}/${HDR_FILE}"
  done <<< $(jq -r '."'${PLF}'"."gen-includes" // {} | keys | .[] | tostring' ${ROOTDIR}/platforms.json)

  # generate missing pkg-config files
  PKGCONFIG=${DATADIR}/pkgconfig
  REQ_PC_FIELDS='{"Name":"","Version":"n/a","Description":"n/a","URL":"n/a"}'
  while read PKG_DESC; do
    if [ -z "${PKG_DESC}" ]; then continue; fi
    mkdir -p ${PKGCONFIG}
    PKG_NAME=$(echo ${PKG_DESC} | jq -r '.Name')
    echo 'gen_includes="GEN_INCLUDES"' > ${PKGCONFIG}/${PKG_NAME}.pc
    echo ${REQ_PC_FIELDS} ${PKG_DESC} \
      | jq -sr 'add | keys_unsorted[] as $k | "\($k): \(.[$k])"' \
      >> ${PKGCONFIG}/${PKG_NAME}.pc
  done <<< $(jq -r '."'${PLF}'"."gen-pkgconfig" // [] | .[] | tostring' ${ROOTDIR}/platforms.json)

  echo ${NON_LOCAL_DEPS} > ${DATADIR}/non_local_deps

  if [ "${PKG}" = "deb" ]; then
    BUILD_DEPENDS=$(jq -r '."'${PLF}'"."build-depends" // [] | map(",\\n  " + .) | join("")' ${ROOTDIR}/platforms.json)
  else
    BUILD_DEPENDS=$(jq -r '."'${PLF}'"."build-depends" // [] | map(", " + .) | join("")' ${ROOTDIR}/platforms.json)
  fi

  if [ "${PKG}" = "deb" ]; then
    COMPAT_LEVEL=$(dpkg -s debhelper | sed -n 's/^Version:\s\+\([0-9]*\).*/\1/p')

    # remove unused files
    find ./debian/ -type f -iname '*.ex' -delete
    rm -f ./debian/{README.source,justbuild-docs.docs}

    # copy prepared debian files
    if [ -z "${TARBALL}" ]; then
      cp -rP ${ROOTDIR}/bootstrapped/debian/* ./debian/
    else
      cp -r ${ROOTDIR}/prebuilt/debian/* ./debian/
      cp ${TARBALL} ./debian/justbuild.tar.gz
      echo debian/justbuild.tar.gz > ./debian/source/include-binaries
    fi

    if [ -f ./debian/justbuild.makefile ]; then
      # set reproducible build path (for cache hits during package rebuilds)
      sed -i 's|BUILDDIR ?= .*|BUILDDIR ?= /tmp/build|g' ./debian/justbuild.makefile

      # use clang if build depends on it
      if echo "${BUILD_DEPENDS}" | grep -q clang; then
        # set FAMILY to "clang" and specify reproducibility compile flag
        sed -i 's/{"FAMILY": "gnu"}/{"FAMILY": "clang"}/' ./debian/justbuild.makefile
        sed -i 's/\([C|CXX]FLAGS +=\)/\1 -fdebug-compilation-dir=. -gdwarf-4 -Wno-ignored-optimization-argument/' ./debian/justbuild.makefile
      fi
    fi

    if [ -d ./debian/third_party ]; then
      mkdir -p ./debian/source
      find debian/third_party -type f -not -name '*.txt' > ./debian/source/include-binaries
    fi

    # remove usused debian files
    rm -f ./debian/README.Debian

    # patch control file
    sed -i 's/COMPAT_LEVEL/'${COMPAT_LEVEL}'/' ./debian/control
    sed -i 's/BUILD_DEPENDS/'"${BUILD_DEPENDS}"'/' ./debian/control

    if [ -f ./debian/upstream/metadata.ex ]; then
      # patch upstream/metadata file
      cat ./debian/upstream/metadata.ex \
        | sed -n 's/#\s\+\(.*<user>.*\)/\1/p' > ./debian/upstream/metadata
      sed -i 's/<user>/'${GH_ORG}'/' ./debian/upstream/metadata
      sed -i 's|master/CHANGES|'${COMMIT_HASH}'/CHANGELOG.md|' ./debian/upstream/metadata
      sed -i 's|wiki|blob/'${COMMIT_HASH}'/README.md#documentation|' ./debian/upstream/metadata
    fi

    # patch changelog file
    CHANGE_DATE=$(date -u -R -d @${SOURCE_DATE_EPOCH})
    sed -i '0,/^\(\ -- .*>\ \ \).*/s//\1'"${CHANGE_DATE}"'/' ./debian/changelog
    if [ -n "${RELEASE}" ]; then
      sed -i 's/UNRELEASED\|unstable/'${RELEASE}'/' ./debian/changelog
    fi

    # build source package
    dpkg-buildpackage -S

    # build binary package
    dpkg-buildpackage -b
  elif [ "${PKG}" = "rpm" ]; then
    # copy prepared rpmbuild files
    if [ -z "${TARBALL}" ]; then
      cp ${ROOTDIR}/bootstrapped/rpmbuild/justbuild.makefile ./rpmbuild/
      cp ${ROOTDIR}/bootstrapped/rpmbuild/justbuild.spec ${HOME}/rpmbuild/SPECS/
      echo ${SOURCE_DATE_EPOCH} > ./rpmbuild/source_date_epoch
    else
      cp ${ROOTDIR}/prebuilt/rpmbuild/justbuild.spec ${HOME}/rpmbuild/SPECS/
      cp ${TARBALL} ./rpmbuild/justbuild.tar.gz
    fi

    # patch spec file
    sed -i 's/VERSION/'${VERSION}'/' ${HOME}/rpmbuild/SPECS/justbuild.spec
    sed -i 's/BUILD_DEPENDS/'"${BUILD_DEPENDS}"'/' ${HOME}/rpmbuild/SPECS/justbuild.spec

    # create archive
    cd ${WORKDIR}
    tar -czf ${HOME}/rpmbuild/SOURCES/${NAME}-${VERSION}.tar.gz ${NAME}-${VERSION}

    # build source rpm
    rpmbuild -bs ${HOME}/rpmbuild/SPECS/justbuild.spec

    # build binary rpm from source rpm
    rpmbuild --rebuild ${HOME}/rpmbuild/SRPMS/justbuild-${VERSION}*.src.rpm
  fi
)
