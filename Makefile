AR = ar
CC = gcc
CFLAGS ?= -Wall -Os -fPIC
LDFLAGS = -L.
SHLIB_CFLAGS = -shared

INSTALL_EXEC = install -m 755 -o root -g root
INSTALL_DATA = install -m 644 -o root -g root
COPY_PRESERVELINKS = cp -d
INSTALL_SOLINKS = $(COPY_PRESERVELINKS)

MAJOR = 0
MINOR = 7
MICRO = 0

NAME = json
A_TARGETS = lib$(NAME).a
BIN_TARGETS = $(NAME)lint
PC_TARGET = lib$(NAME).pc
SO_LINKS = lib$(NAME).so lib$(NAME).so.$(MAJOR) lib$(NAME).so.$(MAJOR).$(MINOR)
SO_FILE = lib$(NAME).so.$(MAJOR).$(MINOR).$(MICRO)
HEADERS = $(NAME).h

PREFIX ?= /usr

TARGETS = $(A_TARGETS) $(SO_FILE) $(SO_LINKS) $(BIN_TARGETS)

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
	$(CC) $(CFLAGS) $(LDFLAGS) -Wl,-soname -Wl,lib$(NAME).so.$(MAJOR).$(MINOR).$(MICRO) $(SHLIB_CFLAGS) -o $@ $^

$(NAME)lint: $(NAME)lint.o $(NAME).o
	$(CC) $(CFLAGS) -o $@ $+

%.o: %.c %.h
	$(CC) $(CFLAGS) -c -o $@ $<

.PHONY: lib$(NAME).pc
lib$(NAME).pc: lib$(NAME).pc.in
	sed -e 's;@PREFIX@;$(PREFIX);' -e 's;@LIB$(NAME)_VER_MAJOR@;$(MAJOR);' -e 's;@LIB$(NAME)_VER_MINOR@;$(MINOR);' < $< > $@

.PHONY: tests clean install install-bin install-lib
tests: $(NAME)lint
	(cd tests; ./runtest)

install-lib: $(SO_TARGETS) $(A_TARGETS) $(PC_TARGET)
	mkdir -p $(PREFIX)/lib/pkgconfig
	$(INSTALL_DATA) -t $(PREFIX)/lib/pkgconfig $(PC_TARGET)
	mkdir -p $(PREFIX)/include
	$(INSTALL_DATA) -t $(PREFIX)/include $(HEADERS)
	mkdir -p $(PREFIX)/lib
	$(INSTALL_EXEC) -t $(PREFIX)/lib $(SO_FILE) $(A_TARGETS)
	$(INSTALL_SOLINKS) $(SO_LINKS) $(PREFIX)/lib

install-bin: $(BIN_TARGETS)
	mkdir -p $(PREFIX)/bin
	$(INSTALL_EXEC) -t $(PREFIX)/bin $(BIN_TARGETS)

install: install-lib install-bin

clean:
	rm -f *.o $(TARGETS)
