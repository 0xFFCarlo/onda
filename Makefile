SRC := $(wildcard src/*.c)
TESTS := $(wildcard tests/*.c)

SRC_NO_MAIN := $(filter-out src/main.c,$(SRC))

all:
	mkdir -p bin
	gcc -g -O3 $(SRC) -o bin/ondac
	gcc -g -O3 $(SRC_NO_MAIN) $(TESTS) -o bin/tests_ondac

