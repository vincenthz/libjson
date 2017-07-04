AR = ar
CC = gcc
CXX = g++
CFLAGS ?= -Wall -Os -fPIC
CFLAGS_TEST ?= -I./unit-tests/catch
LDFLAGS = -L.
SHLIB_CFLAGS = -shared

INSTALL_EXEC = install -m 755
INSTALL_DATA = install -m 644
COPY_PRESERVELINKS = cp -d
INSTALL_SOLINKS = $(COPY_PRESERVELINKS)

MAJOR = 1
MINOR = 0
MICRO = 0

NAME = json
A_TARGETS = lib$(NAME).a
BIN_TARGETS = $(NAME)lint
PC_TARGET = lib$(NAME).pc
SO_LINKS = lib$(NAME).so lib$(NAME).so.$(MAJOR) lib$(NAME).so.$(MAJOR).$(MINOR)
SO_FILE = lib$(NAME).so.$(MAJOR).$(MINOR).$(MICRO)
HEADERS = $(NAME).h

TEST_SRC=$(wildcard unit-tests/*.cpp)

PREFIX ?= /usr
DESTDIR ?=
INSTALLDIR ?= $(DESTDIR)$(PREFIX)

uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo not')

ifeq ($(uname_S),Darwin)
SONAME=
else
SONAME=-Wl,-soname -Wl,lib$(NAME).so.$(MAJOR).$(MINOR).$(MICRO)
endif

TARGETS = $(A_TARGETS) $(SO_FILE) $(SO_LINKS) $(BIN_TARGETS) $(PC_TARGET)

all: $(TARGETS)

lib$(NAME).a: $(NAME).o
	$(AR) rc $@ $+

lib$(NAME).so: lib$(NAME).so.$(MAJOR)
	ln -sf $< $@

lib$(NAME).so.$(MAJOR): lib$(NAME).so.$(MAJOR).$(MINOR)
	ln -sf $< $@

lib$(NAME).so.$(MAJOR).$(MINOR): lib$(NAME).so.$(MAJOR).$(MINOR).$(MICRO)
	ln -sf $< $@

lib$(NAME).so.$(MAJOR).$(MINOR).$(MICRO): $(NAME).o
	$(CC) $(CFLAGS) $(LDFLAGS) $(SONAME) $(SHLIB_CFLAGS) -o $@ $^

$(NAME)lint: $(NAME)lint.o $(NAME).o
	$(CC) $(CFLAGS) -o $@ $+

%.o: %.c %.h
	$(CC) $(CFLAGS) -c -o $@ $<

.PHONY: lib$(NAME).pc
lib$(NAME).pc: lib$(NAME).pc.in
	sed -e 's;@PREFIX@;$(PREFIX);' -e 's;@LIBJSON_VER_MAJOR@;$(MAJOR);' -e 's;@LIBJSON_VER_MINOR@;$(MINOR);' < $< > $@

.PHONY: tests clean install install-bin install-lib unit-tests
tests: $(NAME)lint
	(cd tests; ./runtest)

unit-tests: $(TEST_SRC) $(NAME).o
	$(CXX) $(CFLAGS) $(CFLAGS_TEST) $(LDFLAGS) -o run-unit-tests $+
	./run-unit-tests -d yes

install-lib: $(SO_TARGETS) $(A_TARGETS) $(PC_TARGET)
	mkdir -p $(INSTALLDIR)/lib/pkgconfig
	$(INSTALL_DATA) -t $(INSTALLDIR)/lib/pkgconfig $(PC_TARGET)
	mkdir -p $(INSTALLDIR)/include
	$(INSTALL_DATA) -t $(INSTALLDIR)/include $(HEADERS)
	mkdir -p $(INSTALLDIR)/lib
	$(INSTALL_EXEC) -t $(INSTALLDIR)/lib $(SO_FILE)
	$(INSTALL_DATA) -t $(INSTALLDIR)/lib $(A_TARGETS)
	$(INSTALL_SOLINKS) $(SO_LINKS) $(INSTALLDIR)/lib

install-bin: $(BIN_TARGETS)
	mkdir -p $(INSTALLDIR)/bin
	$(INSTALL_EXEC) -t $(INSTALLDIR)/bin $(BIN_TARGETS)

install: install-lib install-bin

clean:
	rm -f *.o $(TARGETS)
