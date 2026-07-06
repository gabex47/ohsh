PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
DESTDIR ?=
OHSH_SRC_DIR ?= $(CURDIR)

ifeq ($(OS),Windows_NT)
CC = gcc
EXE ?= ohsh.exe
PLATFORM_SRC = src/platform/windows.c
RUN_CMD = .\$(EXE)
INSTALL_CMD = powershell -NoProfile -Command "New-Item -ItemType Directory -Force -Path '$(DESTDIR)$(BINDIR)' | Out-Null; Copy-Item -Force '$(EXE)' '$(DESTDIR)$(BINDIR)\ohsh.exe'"
UNINSTALL_CMD = powershell -NoProfile -Command "Remove-Item -Force '$(DESTDIR)$(BINDIR)\ohsh.exe' -ErrorAction SilentlyContinue"
CLEAN_CMD = powershell -NoProfile -Command "Remove-Item -Force '$(EXE)' -ErrorAction SilentlyContinue"
else
CC ?= cc
EXE ?= ohsh
PLATFORM_SRC = src/platform/unix.c
RUN_CMD = ./$(EXE)
INSTALL_CMD = install -d "$(DESTDIR)$(BINDIR)" && install -m 0755 "$(EXE)" "$(DESTDIR)$(BINDIR)/ohsh"
UNINSTALL_CMD = rm -f "$(DESTDIR)$(BINDIR)/ohsh"
CLEAN_CMD = rm -f "$(EXE)"
endif

CFLAGS ?= -Wall -Wextra -O2
CPPFLAGS += -DOHSH_SRC_DIR=\"$(OHSH_SRC_DIR)\"
LDFLAGS ?=

SRC = src/main.c src/lexer.c src/parser.c src/executor.c $(PLATFORM_SRC)
HEADERS = src/lexer.h src/parser.h src/executor.h src/platform/platform.h
OUT = $(EXE)

.PHONY: all build run install uninstall clean test sanitize

all: build

build: $(OUT)

$(OUT): $(SRC) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) $(LDFLAGS) -o $(OUT)

run: build
	$(RUN_CMD)

install: build
	$(INSTALL_CMD)

uninstall:
	$(UNINSTALL_CMD)

test: build
	./tests/run-tests.sh "$(RUN_CMD)"

sanitize:
	$(MAKE) clean
	$(MAKE) build CFLAGS="-Wall -Wextra -O1 -g -fsanitize=address,undefined" LDFLAGS="-fsanitize=address,undefined"
	./tests/run-tests.sh "$(RUN_CMD)"

clean:
	$(CLEAN_CMD)
