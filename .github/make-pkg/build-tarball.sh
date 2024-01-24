#!/bin/bash

set -eu

: ${BUILDDIR:=/tmp/build}

NAME=justbuild-$VERSION-$TARGET_ARCH-linux
OUTDIR=$(pwd)/../$NAME

if [ "$TARGET_ARCH" = "aarch64" ]; then
  TARGET_ARCH="arm64"
fi

cat > build.conf << EOF
{ "TOOLCHAIN_CONFIG": {"FAMILY": "gnu"}
, "TARGET_ARCH": "$TARGET_ARCH"
, "AR": "ar"
, "BUILD_STATIC_BINARY": true
, "SOURCE_DATE_EPOCH": $SOURCE_DATE_EPOCH
}
EOF

sed -i 's/"to_git": true/"to_git": false/g' etc/repos.json
if ldd 2>&1 | grep musl >/dev/null; then
  sed -i 's/linux-gnu/linux-musl/g' etc/toolchain/CC/TARGETS
fi

if just-mr version >/dev/null 2>&1; then
  # build with justbuild
  just-mr --no-fetch-ssl-verify --local-build-root ${BUILDDIR}/.just install \
    -c build.conf -o ${OUTDIR} 'installed just'
  just-mr --no-fetch-ssl-verify --local-build-root ${BUILDDIR}/.just install \
    -c build.conf -o ${OUTDIR} 'installed just-mr'
else
  # build via full bootstrap
  export JUST_BUILD_CONF="$(cat build.conf)"
  python3 ./bin/bootstrap.py . ${BUILDDIR} ${DISTFILES}
  ${BUILDDIR}/out/bin/just install \
    --local-build-root ${BUILDDIR}/.just \
    -C ${BUILDDIR}/repo-conf.json \
    -c ${BUILDDIR}/build-conf.json \
    -o ${BUILDDIR}/out/ 'installed just-mr'
  install -D ${BUILDDIR}/out/bin/just ${OUTDIR}/bin/just
  install -D ${BUILDDIR}/out/bin/just-mr ${OUTDIR}/bin/just-mr
fi

for f in $(ls ./share/man/); do
  OUTFILE=$(basename $f .md)
  SECTION="man$(echo $f | sed 's/[^0-9]//g')"
  pandoc -s -t man ./share/man/$f -o ${OUTFILE}
  install -D ${OUTFILE} ${OUTDIR}/share/man/$SECTION/$OUTFILE
done

install -D ./bin/just-import-git.py ${OUTDIR}/bin/just-import-git
install -D ./share/just_complete.bash ${OUTDIR}/share/bash-completion/completions/just

cd $OUTDIR/..
tar --sort=name --owner=root:0 --group=root:0 --mtime='UTC 1970-01-01' -czvf $NAME.tar.gz $NAME
