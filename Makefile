AR = ar
CC = gcc
CFLAGS ?= -Wall -Os -fPIC
LDFLAGS = -L.
SHLIB_CFLAGS = -shared

INSTALL_EXEC = install -m 755 -o root -g root
INSTALL_DATA = install -m 644 -o root -g root

MAJOR = 0
MINOR = 7
MICRO = 0

A_TARGETS = libjson.a
SO_TARGETS = libjson.so libjson.so.$(MAJOR) libjson.so.$(MAJOR).$(MINOR) libjson.so.$(MAJOR).$(MINOR).$(MICRO)
BIN_TARGETS = jsonlint
PC_TARGET = libjson.pc

PREFIX ?=

TARGETS = $(A_TARGETS) $(SO_TARGETS) $(BIN_TARGETS)

all: $(TARGETS)

libjson.a: json.o
	$(AR) rc $@ $+

libjson.so: libjson.so.$(MAJOR)
	ln -sf $< $@

libjson.so.$(MAJOR): libjson.so.$(MAJOR).$(MINOR)
	ln -sf $< $@

libjson.so.$(MAJOR).$(MINOR): libjson.so.$(MAJOR).$(MINOR).$(MICRO)
	ln -sf $< $@

libjson.so.$(MAJOR).$(MINOR).$(MICRO): json.o
	$(CC) $(CFLAGS) $(LDFLAGS) -Wl,-soname -Wl,libjson.so.$(MAJOR).$(MINOR).$(MICRO) $(SHLIB_CFLAGS) -o $@ $^

jsonlint: jsonlint.o json.o
	$(CC) $(CFLAGS) -o $@ $+

%.o: %.c %.h
	$(CC) $(CFLAGS) -c -o $@ $<

.PHONY: libjson.pc
libjson.pc: libjson.pc.in
	sed -e 's;@PREFIX@;/usr;' -e 's;@LIBJSON_VER_MAJOR@;$(MAJOR);' -e 's;@LIBJSON_VER_MINOR@;$(MINOR);' < $< > $@

.PHONY: tests clean install install-bin install-lib
tests: jsonlint
	(cd tests; ./runtest)

install-lib: $(SO_TARGETS) $(A_TARGETS) $(PC_TARGET)
	mkdir -p $(PREFIX)/usr/lib/pkgconfig
	$(INSTALL_DATA) -t $(PREFIX)/usr/lib/pkgconfig $(PC_TARGET)
	mkdir -p $(PREFIX)/usr/include
	$(INSTALL_DATA) -t $(PREFIX)/usr/include json.h
	mkdir -p $(PREFIX)/usr/lib
	$(INSTALL_EXEC) -t $(PREFIX)/usr/lib $(SO_TARGETS) $(A_TARGETS)

install-bin: $(BIN_TARGETS)
	mkdir -p $(PREFIX)/usr/bin
	$(INSTALL_EXEC) -t $(PREFIX)/usr/bin $(BIN_TARGETS)

install: install-lib install-bin

clean:
	rm -f *.o $(TARGETS)
