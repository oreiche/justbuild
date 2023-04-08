#!/bin/bash

set -euo pipefail

readonly TEMP=$(mktemp -d)
readonly REF="${1:-HEAD}"
shift

readonly ROOTDIR=$(realpath $(dirname $0))
FAIL_COUNT=0

function docker_build() {
  local NAME=$1
  local PKG=$(jq -r '."'${NAME}'".type' ${ROOTDIR}/platforms.json)
  local IMAGE=$(jq -r '."'${NAME}'".image' ${ROOTDIR}/platforms.json)
  local BUILD_DEPS=$(jq -r '."'${NAME}'"."build-depends" | join(" ")' \
                        ${ROOTDIR}/platforms.json)
  local DOCKER_ARGS="-w /workspace -v $(pwd):/workspace"

  if [ -t 0 ]; then
    DOCKER_ARGS="-it ${DOCKER_ARGS}"
  fi

  # generate docker file
  mkdir -p ${TEMP}
  if [ "${PKG}" = "deb" ]; then
    cat > ${TEMP}/Dockerfile.${NAME} << EOL
FROM ${IMAGE}
ENV HOME=/tmp/nobody
ENV DEBIAN_FRONTEND=noninteractive
RUN apt update && apt install -y jq git wget dh-make
RUN apt install -y ${BUILD_DEPS}
EOL
  elif [ "${PKG}" = "rpm" ]; then
    cat > ${TEMP}/Dockerfile.${NAME} << EOL
FROM ${IMAGE}
ENV HOME=/tmp/nobody
RUN dnf install -y jq git wget make rpmdevtools
RUN dnf install -y ${BUILD_DEPS}
EOL
  else
    echo "Unsupported pkg type '${PKG}'"
    exit 1
  fi

  # build docker image
  docker build -f ${TEMP}/Dockerfile.${NAME} -t just-make-${PKG}:${NAME} ${TEMP}

  # build package
  docker run ${DOCKER_ARGS} -u $(id -u):$(id -g) just-make-${PKG}:${NAME} \
    .github/make-pkg/build.sh ${REF} ${NAME} ${PKG}

  if [ "${PKG}" = "deb" ] && [ -f ./work_${NAME}/justbuild_*.deb ]; then
    # verify deb package
    docker run ${DOCKER_ARGS} ${IMAGE} /bin/bash -c "\
      set -e; \
      export DEBIAN_FRONTEND=noninteractive; \
      apt update; \
      apt install --no-install-recommends -y ./work_${NAME}/justbuild_*.deb; \
      just version && just-mr mrversion; \
      if [ $? = 0 ]; then touch ./work_${NAME}/success; fi"
  elif [ "${PKG}" = "rpm" ] && [ -f ./work_${NAME}/rpmbuild/RPMS/x86_64/justbuild-*.rpm ]; then
    # verify rpm package
    docker run ${DOCKER_ARGS} ${IMAGE} /bin/bash -c "\
      set -e; \
      dnf install -y ./work_${NAME}/rpmbuild/RPMS/x86_64/justbuild-*rpm; \
      just version && just-mr mrversion; \
      if [ $? = 0 ]; then touch ./work_${NAME}/success; fi"
  fi
}

function report() {
  local NAME=$1
  if [ -f work_${NAME}/success ]; then
    echo PASS: ${NAME}
  else
    echo FAIL: ${NAME}
    FAIL_COUNT=$((${FAIL_COUNT}+1))
  fi
}

if [ "${#@}" = 0 ]; then
  # build all if not specified otherwise
  set -- $(jq -r 'keys | .[]' "${ROOTDIR}/platforms.json" | xargs)
fi

( set +e
  for PLATFORM in "$@"; do
    docker_build ${PLATFORM}
  done
  exit 0
)

for PLATFORM in "$@"; do
  report ${PLATFORM}
done

rm -rf ${TEMP}

exit ${FAIL_COUNT}
