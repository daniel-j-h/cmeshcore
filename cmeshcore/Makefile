CFLAGS+=-std=c99 -O2 -march=x86-64-v3 -Wall -Wextra -pedantic -fvisibility=hidden -ffunction-sections -fPIC -MMD
LDFLAGS+=-Wl,--gc-sections
LDLIBS+=-lm
PREFIX?=/usr/local

SRC:=$(wildcard *.c)
OBJ:=$(SRC:.c=.o)
DEP:=$(SRC:.c=.d)

.PHONY: all
all: libcmeshcore.so cmeshcore-cli

.PHONY: install
install: libcmeshcore.so
	mkdir -p $(PREFIX)/include/cmeshcore
	install -m 0644 cmeshcore.h $(PREFIX)/include/cmeshcore
	mkdir -p $(PREFIX)/lib
	install -m 0644 libcmeshcore.so $(PREFIX)/lib
	ln -sf $(PREFIX)/lib/libcmeshcore.so $(PREFIX)/lib/libcmeshcore.so.0
	ln -sf $(PREFIX)/lib/libcmeshcore.so $(PREFIX)/lib/libcmeshcore.so.0.0

-include ${DEP}

libcmeshcore.so: CFLAGS+=-DNDEBUG
libcmeshcore.so: LDFLAGS+=-shared -Wl,-soname,libcmeshcore.so.0
libcmeshcore.so: $(filter-out cmeshcore-cli.o, $(OBJ))
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)
	@ln -sf libcmeshcore.so libcmeshcore.so.0

cmeshcore-cli: LDFLAGS+=-Wl,-rpath=.:cmeshcore
cmeshcore-cli: libcmeshcore.so

.PHONY: watch
watch:
	@while true; do \
		inotifywait -q -e modify -e create -e delete -e move *.c *.h Makefile; \
		make --no-print-directory; \
	done

.PHONY: clean
clean:
	@rm -f *.o *.d *.so* cmeshcore-cli
