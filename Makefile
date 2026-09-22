PREFIX  ?= $(HOME)/.local
BINDIR  ?= $(PREFIX)/bin
MANDIR  ?= $(PREFIX)/share/man/man1
CC      ?= cc
CFLAGS  ?= -O2 -g
CFLAGS  += -std=gnu11 -Wall -Wextra -Wshadow -Wno-unused-parameter

SRC := $(wildcard src/*.c)
OBJ := $(SRC:src/%.c=build/%.o)

all: build/sw

build/sw: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ)

build/%.o: src/%.c src/sw.h | build
	$(CC) $(CFLAGS) -c -o $@ $<

build:
	mkdir -p build

install: build/sw
	install -d $(DESTDIR)$(BINDIR) $(DESTDIR)$(MANDIR)
	install -m 755 build/sw $(DESTDIR)$(BINDIR)/sw
	ln -sf sw $(DESTDIR)$(BINDIR)/switch
	install -m 644 man/sw.1 $(DESTDIR)$(MANDIR)/sw.1
	ln -sf sw.1 $(DESTDIR)$(MANDIR)/switch.1

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/sw $(DESTDIR)$(BINDIR)/switch
	rm -f $(DESTDIR)$(MANDIR)/sw.1 $(DESTDIR)$(MANDIR)/switch.1

test: build/sw
	sh tests/run.sh

clean:
	rm -rf build

.PHONY: all install uninstall test clean
