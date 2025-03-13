PREFIX ?= /usr
DATADIR ?= $(CURDIR)/debian
BUILDDIR ?= $(DATADIR)/build
DISTFILES ?= $(DATADIR)/third_party

ifeq ($(shell uname -m),aarch64)
  ARCH ?= arm64
else
  ARCH ?= x86_64
endif
TARGET_ARCH ?= $(ARCH)

export LOCALBASE = /usr
export NON_LOCAL_DEPS = $(shell cat $(DATADIR)/non_local_deps)
export SOURCE_DATE_EPOCH = $(shell dpkg-parsechangelog -STimestamp)
export INCLUDE_PATH = $(BUILDDIR)/include
export PKG_CONFIG_PATH = $(BUILDDIR)/pkgconfig

CFLAGS += -I$(INCLUDE_PATH)
CXXFLAGS += -I$(INCLUDE_PATH)

define JUST_BUILD_CONF
{ "TOOLCHAIN_CONFIG": {"FAMILY": "gnu"}
, "ARCH": "$(ARCH)"
, "TARGET_ARCH": "$(TARGET_ARCH)"
, "SOURCE_DATE_EPOCH": $(SOURCE_DATE_EPOCH)
, "ADD_CFLAGS": [$(shell printf '"%s"\n' $(CFLAGS) | paste -sd,)]
, "ADD_CXXFLAGS": [$(shell printf '"%s"\n' $(CXXFLAGS) | paste -sd,)]
}
endef
export JUST_BUILD_CONF

# set dummy proxy to prevent _any_ downloads from happening during bootstrap
export http_proxy = http://8.8.8.8:3128
export https_proxy = http://8.8.8.8:3128

# copy man sources and rename just.1.md to justbuild.1.md
$(shell mkdir -p $(BUILDDIR)/man \
        && cp ./share/man/*.md $(BUILDDIR)/man \
        && mv $(BUILDDIR)/man/just.1.md $(BUILDDIR)/man/justbuild.1.md)

MDFILES := $(wildcard $(BUILDDIR)/man/*.md)
MANPAGES := $(addprefix $(BUILDDIR)/, $(patsubst %.md, %, $(MDFILES)))


all: justbuild man-pages

$(INCLUDE_PATH):
	@mkdir -p $@
	if [ -d $(DATADIR)/include ]; then \
	  cp -r $(DATADIR)/include/. $@; \
	fi

$(PKG_CONFIG_PATH):
	@mkdir -p $@
	if [ -d $(DATADIR)/pkgconfig ]; then \
	  cp -r $(DATADIR)/pkgconfig/. $@; \
	  find $@ -type f -exec sed 's|GEN_INCLUDES|'$(INCLUDE_PATH)'|g' -i {} \;; \
	fi

$(BUILDDIR)/out/bin/just: $(PKG_CONFIG_PATH) $(INCLUDE_PATH)
	env PACKAGE=YES python3 ./bin/bootstrap.py . $(BUILDDIR) $(DISTFILES)
	@touch $@

$(BUILDDIR)/out/bin/just-mr: $(BUILDDIR)/out/bin/just
	$(BUILDDIR)/out/bin/just install \
	  --local-build-root $(BUILDDIR)/.just \
	  -C $(BUILDDIR)/repo-conf.json \
	  -c $(BUILDDIR)/build-conf.json \
	  -o $(BUILDDIR)/out/ 'installed just-mr'
	@touch $@

$(BUILDDIR)/%: %.md
	@mkdir -p $(@D)
	pandoc -s -t man $< -o $@

justbuild: $(BUILDDIR)/out/bin/just $(BUILDDIR)/out/bin/just-mr

man-pages: $(MANPAGES)
	mkdir -p $(DATADIR)/man
	cp $(MANPAGES) $(DATADIR)/man/
	$(foreach m, $(MANPAGES), \
	  $(shell echo debian/man/$$(basename $(m)) >> $(DATADIR)/justbuild.manpages))

install: justbuild
	install -D $(BUILDDIR)/out/bin/just $(DESTDIR)/$(PREFIX)/bin/justbuild
	install -D $(BUILDDIR)/out/bin/just-mr $(DESTDIR)/$(PREFIX)/bin/just-mr
	install -D ./bin/just-lock.py $(DESTDIR)/$(PREFIX)/bin/just-lock
	install -D ./bin/just-import-git.py $(DESTDIR)/$(PREFIX)/bin/just-import-git
	install -D ./bin/just-deduplicate-repos.py $(DESTDIR)/$(PREFIX)/bin/just-deduplicate-repos
	install -D -m 644 ./share/just_complete.bash $(DESTDIR)/$(PREFIX)/share/bash-completion/completions/justbuild

clean:
	rm -rf $(BUILDDIR)/*

distclean: clean
	rm -rf $(BUILDDIR)

.PHONY: all justbuild man-pages install clean distclean
