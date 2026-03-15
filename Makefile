SRC := $(wildcard src/*.c)
TESTS := $(wildcard tests/*.c)

SRC_NO_MAIN := $(filter-out src/main.c,$(SRC))
PREFIX ?= $(HOME)/.local
BINDIR ?= $(PREFIX)/bin

all:
	mkdir -p bin
	gcc -g -O3 -Wall -Wextra -Wstrict-prototypes -Wmissing-prototypes -Wwrite-strings -Wundef -Wimplicit-fallthrough -Wnull-dereference $(SRC) -o bin/ondac
	gcc -g -O3 -Wall $(SRC_NO_MAIN) $(TESTS) -o bin/tests_ondac

install: all
	mkdir -p $(BINDIR)
	cp bin/ondac $(BINDIR)/ondac
	ln -sf $(BINDIR)/ondac $(BINDIR)/onda

uninstall:
	rm -f $(BINDIR)/ondac $(BINDIR)/onda
