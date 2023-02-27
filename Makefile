##
# dwl-state
#
# @file
# @version 1.0
.POSIX:
.SUFFIXES:

VERSION    = 1.0
PKG_CONFIG = pkg-config

#paths
PREFIX = /usr/local
MANDIR = $(PREFIX)/share/man

# Compile flags
CC 		  = gcc
PKGS      = wayland-client wayland-cursor pangocairo
BARCFLAGS = `$(PKG_CONFIG) --cflags $(PKGS)` $(CFLAGS)
BARLIBS   = `$(PKG_CONFIG) --libs $(PKGS)` $(LIBS)

# Wayland-scanner
WAYLAND_SCANNER   = `$(PKG_CONFIG) --variable=wayland_scanner wayland-scanner`
WAYLAND_PROTOCOLS = `$(PKG_CONFIG) --variable=pkgdatadir wayland-protocols`

srcdir := src

all: dwl-state
dwl-state: $(srcdir)/xdg-output-unstable-v1-protocol.o $(srcdir)/dwl-bar-ipc-unstable-v1-protocol.o $(srcdir)/dwl-state.c
	$(CC) $^ $(BARLIBS) $(BARCFLAGS) -o $@
$(srcdir)/%.o: $(srcdir)/%.c $(srcdir)/%.h
	$(CC) -c $< $(BARLIBS) $(BARCFLAGS) -o $@

$(srcdir)/xdg-output-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) client-header \
		$(WAYLAND_PROTOCOLS)/unstable/xdg-output/xdg-output-unstable-v1.xml $@
$(srcdir)/xdg-output-unstable-v1-protocol.c:
	$(WAYLAND_SCANNER) private-code \
		$(WAYLAND_PROTOCOLS)/unstable/xdg-output/xdg-output-unstable-v1.xml $@

$(srcdir)/dwl-bar-ipc-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) client-header \
		protocols/dwl-bar-ipc-unstable-v1.xml $@
$(srcdir)/dwl-bar-ipc-unstable-v1-protocol.c:
	$(WAYLAND_SCANNER) private-code \
		protocols/dwl-bar-ipc-unstable-v1.xml $@

$(srcdir)/config.h:
	cp src/config.def.h $@

clean:
	rm -f dwl-state src/*.o src/*-protocol.*

dist: clean
	mkdir -p dwl-state-$(VERSION)
	cp -R LICENSE Makefile README.md src protocols \
		dwl-state-$(VERSION)
	tar -caf dwl-state-$(VERSION).tar.gz dwl-state-$(VERSION)
	rm -rf dwl-state-$(VERSION)

install: dwl-state
	mkdir -p $(PREFIX)/bin
	cp -f dwl-bar $(PREFIX)/bin
	chmod 755 $(PREFIX)/bin/dwl-state
	mkdir -p $(PREFIX)/man1
	cp -f dwl-bar.1 $(MANDIR)/man1
	chmod 644 $(MANDIR)/man1/dwl-state.1

uninstall:
	rm -f $(PREFIX)/bin/dwl-state $(MANDIR)/man1/dwl-state.1

# end
