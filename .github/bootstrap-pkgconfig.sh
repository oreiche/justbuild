#!/bin/bash

set -euo pipefail

# global paths
readonly BUILD_DIR=$(mktemp -d)
readonly DISTFILES=${BUILD_DIR}/externals/distfiles
readonly PKGCONFIG=${BUILD_DIR}/externals/pkgconfig

rm -rf ${BUILD_DIR}

# bootstrap settings
export LOCALBASE=/usr
export JUST_BUILD_CONF='{"COMPILER_FAMILY":"clang"}'
export NON_LOCAL_DEPS='["google_apis","bazel_remote_apis"]'
export GEN_PKG_CONFIG='[{"Name":"gsl","Cflags":"-I/usr/include"}]'
export PKG_CONFIG_PATH=${PKGCONFIG}

# fetch distfiles of non-local deps
mkdir -p ${DISTFILES}
while read DEP; do
  URL="$(jq -r '.repositories."'${DEP}'".repository.fetch' etc/repos.json)"
  wget -nv -P ${DISTFILES} "${URL}"
done <<< $(echo ${NON_LOCAL_DEPS} | jq -r '.[]')

# generate missing pkgconfig files
mkdir -p ${PKGCONFIG}
REQ_PC_FIELDS='{"Name":"","Version":"n/a","Description":"n/a","URL":"n/a"}'
while read PKG_DESC; do
  PKG_NAME=$(echo ${PKG_DESC} | jq -r '.Name')
  echo ${REQ_PC_FIELDS} ${PKG_DESC} \
    | jq -sr 'add | keys_unsorted[] as $k | "\($k): \(.[$k])"' \
    > ${PKGCONFIG}/${PKG_NAME}.pc
done <<< $(echo ${GEN_PKG_CONFIG} | jq -r '.[] | tostring')

# set dummy proxy to prevent _any_ downloads during bootstrap
export http_proxy="http://8.8.8.8:3128"
export https_proxy="http://8.8.8.8:3128"

# bootstrap just
env PACKAGE=YES python3 ./bin/bootstrap.py . ${BUILD_DIR} ${DISTFILES}

# build just-mr using bootstrap's repo & build configs
${BUILD_DIR}/out/bin/just install \
  --local-build-root ${BUILD_DIR}/.just \
  -C ${BUILD_DIR}/repo-conf.json \
  -c ${BUILD_DIR}/build-conf.json \
  -o ${BUILD_DIR}/out/ 'installed just-mr'

echo SUCCESS: ${BUILD_DIR}
