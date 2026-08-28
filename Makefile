CC := tcc
CFLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
CPPFLAGS := -Isrc

SOURCES := \
	src/ast.c \
	src/archive.c \
	src/api.c \
	src/sandbox.c \
	src/service.c \
	src/observe.c \
	src/signature.c \
	src/transaction.c \
	src/diag.c \
	src/dependency.c \
	src/exec.c \
	src/fs.c \
	src/identity.c \
	src/lexer.c \
	src/main.c \
	src/manifest.c \
	src/package.c \
	src/parser.c \
	src/runtime.c \
	src/source.c \
	src/validate.c
OBJECTS := $(SOURCES:.c=.o)
TARGET := cbs

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) $(OBJECTS) -larchive -lzstd -o $@

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
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/identity-test.c \
		src/ast.o src/diag.o src/exec.o src/identity.o src/lexer.o src/parser.o \
		src/validate.o -o tests/identity-test
	./tests/identity-test tests/fixtures/execution/identity.cbs
	rm -f tests/identity-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/source-test.c \
		src/ast.o src/diag.o src/exec.o src/lexer.o src/parser.o src/source.o \
		src/validate.o -o tests/source-test
	./tests/source-test tests/fixtures/execution/sources.cbs
	rm -f tests/source-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/fetch-test.c \
		src/ast.o src/diag.o src/exec.o src/lexer.o src/parser.o src/source.o \
		src/validate.o -o tests/fetch-test
	./tests/fetch-test tests/fixtures/execution/sources.cbs
	rm -f tests/fetch-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/archive-test.c src/archive.o src/diag.o \
		-larchive -o tests/archive-test
	./tests/archive-test
	rm -f tests/archive-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/jobs-test.c src/ast.o src/diag.o src/exec.o \
		src/fs.o src/lexer.o src/parser.o src/runtime.o src/validate.o -o tests/jobs-test
	./tests/jobs-test
	rm -f tests/jobs-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/stage-test.c src/runtime.o src/ast.o src/diag.o src/exec.o src/fs.o src/lexer.o src/parser.o src/validate.o -o tests/stage-test
	./tests/stage-test
	rm -f tests/stage-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/manifest-test.c src/manifest.o src/source.o \
		src/ast.o src/diag.o src/exec.o src/lexer.o src/parser.o src/validate.o -o tests/manifest-test
	./tests/manifest-test
	rm -f tests/manifest-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/package-test.c src/package.o src/source.o src/manifest.o \
		src/ast.o src/diag.o src/exec.o src/lexer.o src/parser.o src/validate.o \
		-lzstd -o tests/package-test
	./tests/package-test
	rm -f tests/package-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/repro-test.c src/package.o src/source.o src/manifest.o \
		src/ast.o src/diag.o src/exec.o src/lexer.o src/parser.o src/validate.o \
		-lzstd -o tests/repro-test
	./tests/repro-test
	rm -f tests/repro-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/dependency-test.c \
		src/ast.o src/dependency.o src/diag.o src/exec.o src/lexer.o src/parser.o \
		src/validate.o -o tests/dependency-test
	./tests/dependency-test tests/fixtures/valid/complete.cbs
	rm -f tests/dependency-test

clean:
	rm -f $(OBJECTS) $(TARGET) tests/exec-test tests/fs-test \
		tests/edit-assert-test
	rm -f tests/runtime-test
	rm -f tests/identity-test
	rm -f tests/source-test
	rm -f tests/fetch-test
	rm -f tests/archive-test
	rm -f tests/jobs-test
	rm -f tests/stage-test
	rm -f tests/manifest-test
	rm -f tests/package-test
	rm -f tests/repro-test
	rm -f tests/dependency-test
