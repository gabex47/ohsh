CC ?= cc
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
DESTDIR ?=
OHSH_SRC_DIR ?= $(CURDIR)

CFLAGS ?= -Wall -Wextra -O2
CPPFLAGS += -DOHSH_SRC_DIR=\"$(OHSH_SRC_DIR)\"
LDFLAGS ?=

SRC = src/main.c src/lexer.c src/parser.c src/executor.c
OUT = ohsh

.PHONY: all build run install uninstall clean test

all: build

build: $(OUT)

$(OUT): $(SRC)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) $(LDFLAGS) -o $(OUT)

run: build
	./$(OUT)

install: build
	install -d "$(DESTDIR)$(BINDIR)"
	install -m 0755 "$(OUT)" "$(DESTDIR)$(BINDIR)/ohsh"

uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/ohsh"

test: build
	printf 'examples\nwhere am i\nexit\n' | ./$(OUT)

clean:
	rm -f "$(OUT)"
