CC = gcc
LD = $(CC)

OPTS += -pthread -lfuse -D_FILE_OFFSET_BITS=64
OPTS += -I include
OPTS += -g -Og -std=c99 -Wall -Wextra -Werror -pedantic

SOURCES += src/cmd.c
SOURCES += src/map.c
SOURCES += src/utils.c
SOURCES += src/main.c
SOURCES += LICENSE
SOURCES += README.md

OBJECTS = $(patsubst %,build/%.o,$(SOURCES))

all: bin/fuserescue

bin/fuserescue: $(OBJECTS) | bin
	$(LD) $(OPTS) $^ -o $@

build/src/%.c.o: src/%.c
	mkdir -p "$(dir $@)"
	$(CC) $(OPTS) $^ -c -o $@

build/%.o: %
	file="$^"; \
	id="$$(printf '%s' "$$file"|sed 's/[^a-zA-Z]/_/g'|sed 's/.*/\L\0/')"; \
	( \
	  echo '#include <stddef.h>'; \
	  printf "extern const char %s[];" "$$id"; \
	  printf "extern const size_t %s_size;" "$$id"; \
	  printf "const char %s[] = {" "$$id"; \
	  cat "$$file" | sed 's/\\/\\\\/g' | sed 's/"/\\"/g' | sed 's/.*/  "\0\\n"/'; \
	  printf "};\nconst size_t %s_size = sizeof(%s);\n" "$$id" "$$id"; \
	) | $(LD) -x c - -c -o $@

bin:
	mkdir -p $@

clean:
	rm -rf build bin
