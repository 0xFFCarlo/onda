SRC := $(wildcard src/*.c)
TESTS := $(wildcard tests/*.c)

SRC_NO_MAIN := $(filter-out src/main.c,$(SRC))

all:
	mkdir -p bin
	gcc -g -O0 -Wall -Wextra -Wstrict-prototypes -Wmissing-prototypes -Wwrite-strings -Wundef -Wimplicit-fallthrough -Wnull-dereference $(SRC) -o bin/ondac
	gcc -g -O0 -Wall $(SRC_NO_MAIN) $(TESTS) -o bin/tests_ondac

