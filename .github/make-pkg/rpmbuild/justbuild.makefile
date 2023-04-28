PREFIX ?= /usr
BUILDDIR ?= ../build
DISTFILES ?= ./rpmbuild/distfiles

ifeq ($(shell uname -m),aarch64)
  ARCH ?= arm64
else
  ARCH ?= x86_64
endif
TARGET_ARCH ?= $(ARCH)

export LOCALBASE = /usr
export NON_LOCAL_DEPS = $(shell cat ./rpmbuild/non_local_deps)
export PKG_CONFIG_PATH = $(shell pwd)/rpmbuild/pkgconfig
export SOURCE_DATE_EPOCH = $(shell cat ./rpmbuild/source_date_epoch)

define JUST_BUILD_CONF
{ "COMPILER_FAMILY": "clang"
, "AR": "ar"
, "ARCH": "$(ARCH)"
, "TARGET_ARCH": "$(TARGET_ARCH)"
, "SOURCE_DATE_EPOCH": $(SOURCE_DATE_EPOCH)
, "ADD_CFLAGS": ["-I$(shell pwd)/rpmbuild/includes"]
, "ADD_CXXFLAGS": ["-I$(shell pwd)/rpmbuild/includes"]
}
endef
export JUST_BUILD_CONF

# set dummy proxy to prevent _any_ downloads during bootstrap
export http_proxy = http://8.8.8.8:3128
export https_proxy = http://8.8.8.8:3128

ORGFILES := $(wildcard ./share/man/*.org)
MANPAGES := $(addprefix $(BUILDDIR)/, $(patsubst %.org, %, $(ORGFILES)))


all: justbuild

$(BUILDDIR)/out/bin/just:
	env PACKAGE=YES python3 ./bin/bootstrap.py . $(BUILDDIR) $(DISTFILES)
	@touch $@

$(BUILDDIR)/out/bin/just-mr: $(BUILDDIR)/out/bin/just
	$(BUILDDIR)/out/bin/just install \
	  --local-build-root $(BUILDDIR)/.just \
	  -C $(BUILDDIR)/repo-conf.json \
	  -c $(BUILDDIR)/build-conf.json \
	  -o $(BUILDDIR)/out/ 'installed just-mr'
	@touch $@

$(BUILDDIR)/%: %.org
	@mkdir -p $(@D)
	pandoc -s -t man $< -o $@

justbuild: $(BUILDDIR)/out/bin/just $(BUILDDIR)/out/bin/just-mr

install: justbuild $(MANPAGES)
	install -D $(BUILDDIR)/out/bin/just $(DESTDIR)/$(PREFIX)/bin/just
	install -D $(BUILDDIR)/out/bin/just-mr $(DESTDIR)/$(PREFIX)/bin/just-mr
	install -D ./bin/just-import-git.py $(DESTDIR)/$(PREFIX)/bin/just-import-git
	install -D ./share/just_complete.bash $(DESTDIR)/etc/bash_completion.d/just
	install -D $(BUILDDIR)/share/man/just.1 $(DESTDIR)/$(PREFIX)/share/man/man1/just.1
	install -D $(BUILDDIR)/share/man/just-mr.1 $(DESTDIR)/$(PREFIX)/share/man/man1/just-mr.1
	install -D $(BUILDDIR)/share/man/just-import-git.1 $(DESTDIR)/$(PREFIX)/share/man/man1/just-import-git.1
	install -D $(BUILDDIR)/share/man/just-mrrc.5 $(DESTDIR)/$(PREFIX)/share/man/man5/just-mrrc.5
	install -D $(BUILDDIR)/share/man/just-graph-file.5 $(DESTDIR)/$(PREFIX)/share/man/man5/just-graph-file.5
	install -D $(BUILDDIR)/share/man/just-repository-config.5 $(DESTDIR)/$(PREFIX)/share/man/man5/just-repository-config.5
	install -D $(BUILDDIR)/share/man/just-mr-repository-config.5 $(DESTDIR)/$(PREFIX)/share/man/man5/just-mr-repository-config.5

clean:
	-rm -rf $(BUILDDIR)

distclean: clean

.PHONY: all justbuild install clean distclean
