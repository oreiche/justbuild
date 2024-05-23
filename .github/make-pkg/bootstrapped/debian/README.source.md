# Archives in `debian/third_party`

## Background

The archives contain only [Protocol buffers](https://protobuf.dev/) (an
interface description language) that are meant to be consumed as sources by
[protoc](https://packages.debian.org/de/sid/protobuf-compiler) and
[grpc](https://packages.debian.org/de/bookworm/protobuf-compiler-grpc). These
files are only required during the build process and will not be shipped as part
of the final binary package.

## Security concerns

As these files only contain interface definitions, they themself cannot contain
any bugs or vulnerabilities. Instead, the compilers that consume those files
might be affected by issues, but those are all taken from the official Debian
package repository. Therefore, adding these archives to the source package to be
used as a build dependency should not be subject to any security threats.

## Past and current solutions

Many other Debian packages also include such files, but as part of the upstream
project's source tree. Some even ship a copy of those files in their dev
packages. However, the Justbuild upstream project *does not include any foreign
code* in its source tree. Instead, the Justbuild bootstrap process ensures that
all required foreign code will be fetched (as archives with checksum
verification). To avoid network access, archives can also be provided in a
"distfiles directory". I chose to provide these distfiles in
`debian/third_party`. *All* non-protobuf dependencies are resolved from the
official Debian package repository.
