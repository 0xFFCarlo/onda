all:
		mkdir -p bin
		gcc -g -O0 ./src/*.c -o bin/onda
