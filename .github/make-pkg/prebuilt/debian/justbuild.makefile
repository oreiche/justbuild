PREFIX ?= /usr

install:
	mkdir -p $(DESTDIR)/$(PREFIX)
	tar -xvf debian/justbuild.tar.gz --strip-components=1 -C $(DESTDIR)/$(PREFIX)

.PHONY: install
