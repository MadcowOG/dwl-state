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
SRCDIR = src

# Compile flags
CC 		  = gcc
PKGS      = wayland-client wayland-cursor pangocairo
BARCFLAGS = `$(PKG_CONFIG) --cflags $(PKGS)` $(CFLAGS)
BARLIBS   = `$(PKG_CONFIG) --libs $(PKGS)` $(LIBS)

# Wayland-scanner
WAYLAND_SCANNER   = `$(PKG_CONFIG) --variable=wayland_scanner wayland-scanner`
WAYLAND_PROTOCOLS = `$(PKG_CONFIG) --variable=pkgdatadir wayland-protocols`

# Files
FILES = $(SRCDIR)/dwl-state.c
OBJS  = $(SRCDIR)/xdg-output-unstable-v1-protocol.o $(SRCDIR)/dwl-ipc-unstable-v2-protocol.o

all: dwl-state
dwl-state: $(FILES) $(OBJS)
	$(CC) $^ $(BARLIBS) $(BARCFLAGS) -o $@
$(SRCDIR)/%.o: $(SRCDIR)/%.c $(SRCDIR)/%.h
	$(CC) -c $< $(BARLIBS) $(BARCFLAGS) -o $@

$(SRCDIR)/xdg-output-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) client-header \
		$(WAYLAND_PROTOCOLS)/unstable/xdg-output/xdg-output-unstable-v1.xml $@
$(SRCDIR)/xdg-output-unstable-v1-protocol.c:
	$(WAYLAND_SCANNER) private-code \
		$(WAYLAND_PROTOCOLS)/unstable/xdg-output/xdg-output-unstable-v1.xml $@

$(SRCDIR)/dwl-ipc-unstable-v2-protocol.h:
	$(WAYLAND_SCANNER) client-header \
		protocols/dwl-ipc-unstable-v2.xml $@
$(SRCDIR)/dwl-ipc-unstable-v2-protocol.c:
	$(WAYLAND_SCANNER) private-code \
		protocols/dwl-ipc-unstable-v2.xml $@

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
	cp -f dwl $(PREFIX)/bin
	chmod 755 $(PREFIX)/bin/dwl-state
	mkdir -p $(MANDIR)/man1
	cp -f dwl.1 $(MANDIR)/man1
	chmod 644 $(MANDIR)/man1/dwl-state.1

uninstall:
	rm -f $(PREFIX)/bin/dwl-state $(MANDIR)/man1/dwl-state.1

# end
