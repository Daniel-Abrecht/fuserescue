CC = gcc
LD = $(CC)

OPTS += -pthread -lfuse -D_FILE_OFFSET_BITS=64
OPTS += -I include
OPTS += -g -Og -std=c99 -Wall -Wextra -Werror -pedantic

SOURCES += src/cmd.c
SOURCES += src/map.c
SOURCES += src/utils.c
SOURCES += src/main.c

OBJECTS = $(patsubst src/%.c,build/%.o,$(SOURCES))

all: bin/fuserescue

bin/fuserescue: $(OBJECTS) | bin
	$(LD) $(OPTS) $^ -o $@

build/%.o: src/%.c | build
	$(CC) $(OPTS) $^ -c -o $@

build bin:
	mkdir -p $@

clean:
	rm -rf build bin
