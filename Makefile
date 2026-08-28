CC := tcc
CFLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
CPPFLAGS := -Isrc

SOURCES := \
	src/ast.c \
	src/diag.c \
	src/exec.c \
	src/fs.c \
	src/lexer.c \
	src/main.c \
	src/parser.c \
	src/runtime.c \
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
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/exec-test.c \
		src/ast.o src/diag.o src/exec.o src/lexer.o src/parser.o src/validate.o \
		-o tests/exec-test
	./tests/exec-test tests/fixtures/execution/argv.cbs
	rm -f tests/exec-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/fs-test.c \
		src/ast.o src/diag.o src/exec.o src/fs.o src/lexer.o src/parser.o \
		src/runtime.o src/validate.o -o tests/fs-test
	./tests/fs-test tests/fixtures/execution/filesystem.cbs
	rm -f tests/fs-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/edit-assert-test.c \
		src/ast.o src/diag.o src/exec.o src/fs.o src/lexer.o src/parser.o \
		src/runtime.o src/validate.o -o tests/edit-assert-test
	./tests/edit-assert-test tests/fixtures/execution/edit-assert.cbs
	rm -f tests/edit-assert-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/runtime-test.c \
		src/ast.o src/diag.o src/exec.o src/fs.o src/lexer.o src/parser.o \
		src/runtime.o src/validate.o -o tests/runtime-test
	./tests/runtime-test tests/fixtures/execution/failure.cbs
	rm -f tests/runtime-test

clean:
	rm -f $(OBJECTS) $(TARGET) tests/exec-test tests/fs-test \
		tests/edit-assert-test
	rm -f tests/runtime-test
