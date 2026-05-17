CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -pedantic -Werror -O2 -Ilib/cJSON
LDLIBS = -lm
PREFIX ?= /usr/local
DESTDIR ?=

# To use a custom config, set CONFIG variable: make CONFIG=config_override.h
# Default config.h is included automatically from current directory

rkb: rkb.c lib/cJSON/cJSON.c config.h
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

init-submodules:
	git submodule update --init

clean:
	rm -f rkb

install: rkb
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	install -m 0755 rkb $(DESTDIR)$(PREFIX)/bin/
	mkdir -p $(DESTDIR)$(PREFIX)/share/man/man1
	install -m 0644 rkb.1 $(DESTDIR)$(PREFIX)/share/man/man1/

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/rkb $(DESTDIR)$(PREFIX)/share/man/man1/rkb.1

test: rkb
	./tests/run.sh

memcheck: rkb
	valgrind --leak-check=full --error-exitcode=1 ./tests/run.sh

lint: rkb.c
	cppcheck --std=c99 --enable=all --suppress=missingIncludeSystem rkb.c

.PHONY: init-submodules clean install uninstall test memcheck lint
