CC := tcc
CFLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
CPPFLAGS := -Isrc

SOURCES := \
	src/ast.c \
	src/diag.c \
	src/lexer.c \
	src/main.c \
	src/parser.c \
	src/validate.c
OBJECTS := $(SOURCES:.c=.o)
TARGET := cbs

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) $(OBJECTS) -o $@

src/%.o: src/%.c src/cbs.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

test: $(TARGET)
	./tests/parser-validation.sh ./$(TARGET)

clean:
	rm -f $(OBJECTS) $(TARGET)

