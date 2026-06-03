PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin
LIBDIR  ?= $(PREFIX)/lib
INCDIR  ?= $(PREFIX)/include
UNITDIR ?= /usr/lib/systemd/system

VERSION  = 0.2.0

CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -O2 -std=c11
AR      ?= ar

.PHONY: all clean install uninstall check

all: librtdash/librtdash.a librtdash/librtdash.so rtdashd/rtdashd librtdash/rtdash-ctl librtdash/librtdash.pc

# Static library
librtdash/librtdash.a: librtdash/rtdash.o
	$(AR) rcs $@ $^

# Shared library
librtdash/librtdash.so: librtdash/rtdash.o
	$(CC) -shared -o $@ $^

librtdash/rtdash.o: librtdash/rtdash.c librtdash/rtdash.h librtdash/rtdash_ioctl.h
	$(CC) $(CFLAGS) -fPIC -c -o $@ $<

# CLI tool for ioctl operations (used by dash-activate and rtdashd auto-detect)
librtdash/rtdash-ctl: librtdash/rtdash-ctl.c librtdash/librtdash.a
	$(CC) $(CFLAGS) -o $@ $< -Llibrtdash -lrtdash

# Daemon
rtdashd/rtdashd: rtdashd/rtdashd.c librtdash/librtdash.a
	$(CC) $(CFLAGS) -o $@ $< -Llibrtdash -lrtdash -lpthread

# pkg-config file
librtdash/librtdash.pc: librtdash/librtdash.pc.in
	sed -e "s|@PREFIX@|$(PREFIX)|g" -e "s|@VERSION@|$(VERSION)|g" $< > $@

check:
	bash -n realmanage-cli/realmanage && bash -n dash-activate/dash-activate && echo "scripts OK"

clean:
	rm -f librtdash/*.o librtdash/*.a librtdash/*.so librtdash/rtdash-ctl
	rm -f rtdashd/rtdashd
	rm -f librtdash/librtdash.pc

install: all
	install -Dm755 rtdashd/rtdashd $(DESTDIR)$(BINDIR)/rtdashd
	install -Dm755 librtdash/rtdash-ctl $(DESTDIR)$(BINDIR)/rtdash-ctl
	install -Dm755 realmanage-cli/realmanage $(DESTDIR)$(BINDIR)/realmanage
	install -Dm755 dash-activate/dash-activate $(DESTDIR)$(BINDIR)/dash-activate
	install -Dm644 librtdash/librtdash.a $(DESTDIR)$(LIBDIR)/librtdash.a
	install -Dm755 librtdash/librtdash.so $(DESTDIR)$(LIBDIR)/librtdash.so
	install -Dm644 librtdash/rtdash.h $(DESTDIR)$(INCDIR)/rtdash.h
	install -Dm644 librtdash/rtdash_ioctl.h $(DESTDIR)$(INCDIR)/rtdash_ioctl.h
	install -Dm644 rtdashd/rtdashd.service $(DESTDIR)$(UNITDIR)/rtdashd.service
	install -Dm644 librtdash/librtdash.pc $(DESTDIR)$(LIBDIR)/pkgconfig/librtdash.pc

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/rtdashd
	rm -f $(DESTDIR)$(BINDIR)/rtdash-ctl
	rm -f $(DESTDIR)$(BINDIR)/realmanage
	rm -f $(DESTDIR)$(BINDIR)/dash-activate
	rm -f $(DESTDIR)$(LIBDIR)/librtdash.a
	rm -f $(DESTDIR)$(LIBDIR)/librtdash.so
	rm -f $(DESTDIR)$(INCDIR)/rtdash.h
	rm -f $(DESTDIR)$(INCDIR)/rtdash_ioctl.h
	rm -f $(DESTDIR)$(UNITDIR)/rtdashd.service
	rm -f $(DESTDIR)$(LIBDIR)/pkgconfig/librtdash.pc
