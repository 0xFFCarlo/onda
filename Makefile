CC ?= gcc
ARCH ?= $(shell $(CC) -dumpmachine)

CFLAGS ?= -g -O3 -Wall -Wextra -Wstrict-prototypes -Wmissing-prototypes -Wwrite-strings -Wundef -Wimplicit-fallthrough -Wnull-dereference

SRC_ALL := $(wildcard src/*.c)
TESTS := $(wildcard tests/*.c)
SRC_COMMON := $(filter-out src/onda_jit_x86_64.c src/onda_jit_aarch64.c,$(SRC_ALL))

ARCH_JIT_DEFS := -DONDA_ENABLE_JIT_X86_64=0 -DONDA_ENABLE_JIT_AARCH64=0
JIT_SRC :=

ifneq ($(findstring x86_64,$(ARCH)),)
  JIT_SRC := src/onda_jit_x86_64.c
  ARCH_JIT_DEFS := -DONDA_ENABLE_JIT_X86_64=1 -DONDA_ENABLE_JIT_AARCH64=0
else ifneq ($(findstring aarch64,$(ARCH))$(findstring arm64,$(ARCH)),)
  JIT_SRC := src/onda_jit_aarch64.c
  ARCH_JIT_DEFS := -DONDA_ENABLE_JIT_X86_64=0 -DONDA_ENABLE_JIT_AARCH64=1
endif

CFLAGS += $(ARCH_JIT_DEFS)
SRC := $(SRC_COMMON) $(JIT_SRC)
SRC_NO_MAIN := $(filter-out src/main.c,$(SRC))
PREFIX ?= $(HOME)/.local
BINDIR ?= $(PREFIX)/bin

.PHONY: all tests check clean distclean install uninstall

all: bin/ondac bin/tests_ondac

bin/ondac: $(SRC)
	mkdir -p bin
	$(CC) $(CFLAGS) $(SRC) -o $@

tests check: bin/tests_ondac

bin/tests_ondac: $(SRC_NO_MAIN) $(TESTS)
	mkdir -p bin
	$(CC) $(CFLAGS) $(SRC_NO_MAIN) $(TESTS) -o $@

clean:
	rm -f bin/ondac bin/tests_ondac

distclean: clean

install: bin/ondac
	mkdir -p $(BINDIR)
	cp bin/ondac $(BINDIR)/ondac
	ln -sf $(BINDIR)/ondac $(BINDIR)/onda

uninstall:
	rm -f $(BINDIR)/ondac $(BINDIR)/onda
