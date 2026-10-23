CC ?= cc
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Werror
LDLIBS += -lcurl -lcjson
PREFIX ?= /usr/local
.PHONY: build test install uninstall clean sanitize
build: build/mhc
build/mhc: main.c
	mkdir -p build
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $< $(LDFLAGS) $(LDLIBS)
test: build/mhc
	python3 -m unittest discover -s tests -v
sanitize:
	$(MAKE) clean
	$(MAKE) CFLAGS='-O1 -g -std=c11 -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer' LDFLAGS='-fsanitize=address,undefined' test
install: build/mhc
	install -Dm755 build/mhc $(DESTDIR)$(PREFIX)/bin/mhc
uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/mhc
clean:
	rm -rf build
