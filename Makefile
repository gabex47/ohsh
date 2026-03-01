CC = gcc
CFLAGS = -Wall -g -DOHSH_SRC_DIR=\"$(shell pwd)\"
SRC = src/main.c src/lexer.c src/parser.c src/executor.c
OUT = ohsh

build:
	$(CC) $(CFLAGS) $(SRC) -o $(OUT)

run: build
	./ohsh

clean:
	rm -f $(ohsh)