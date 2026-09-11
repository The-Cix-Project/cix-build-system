CC := tcc
CFLAGS := -std=c11 -Wall -Wextra -Werror -pedantic
CPPFLAGS := -Isrc
CBS_VERSION ?= $(shell sed -n '1p' VERSION 2>/dev/null || printf '%s' 'unknown')
CPPFLAGS += -DCBS_VERSION=\"$(CBS_VERSION)\"

SOURCES := \
	src/ast.c \
	src/archive.c \
	src/cixpkg.c \
	src/api.c \
	src/sandbox.c \
	src/service.c \
	src/observe.c \
	src/signature.c \
	src/diag.c \
	src/dependency.c \
	src/exec.c \
	src/fetch.c \
	src/fs.c \
	src/identity.c \
	src/lexer.c \
	src/main.c \
	src/manifest.c \
	src/package.c \
	src/plan.c \
	src/workspace.c \
	src/parser.c \
	src/runtime.c \
	src/source.c \
	src/validate.c
OBJECTS := $(SOURCES:.c=.o)
TARGET := cbs
LIBRARY := libcbs.a
LIB_OBJECTS := $(filter-out src/main.o,$(OBJECTS))
PREFIX ?= /usr/local
INSTALL ?= install

.PHONY: all clean test install upstream-test qualification-test

all: $(TARGET) $(LIBRARY)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) $(OBJECTS) -larchive -lzstd -ldl -o $@

$(LIBRARY): $(LIB_OBJECTS)
	ar rcs $@ $(LIB_OBJECTS)

install: $(TARGET) $(LIBRARY)
	$(INSTALL) -d $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(PREFIX)/lib \
		$(DESTDIR)$(PREFIX)/include/cbs
	$(INSTALL) -m 755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/cbs
	$(INSTALL) -m 644 $(LIBRARY) $(DESTDIR)$(PREFIX)/lib/$(LIBRARY)
	$(INSTALL) -m 644 src/cbs.h $(DESTDIR)$(PREFIX)/include/cbs/cbs.h

src/%.o: src/%.c src/cbs.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

test: $(TARGET) upstream-test
	./tests/parser-validation.sh ./$(TARGET)
	./tests/recipe-metadata-test.sh cbs.cbs
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/http-server.c -o tests/http-server
	HTTP_SERVER=./tests/http-server ./tests/cli-build-test.sh ./$(TARGET)
	rm -f tests/http-server
	./tests/cli-contract-test.sh ./$(TARGET)
	./tests/stdout-test.sh ./$(TARGET)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/exec-test.c \
		src/ast.o src/diag.o src/exec.o src/lexer.o src/parser.o src/validate.o \
		-o tests/exec-test
	./tests/exec-test tests/fixtures/execution/argv.cbs
	rm -f tests/exec-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/fs-test.c \
		src/ast.o src/archive.o src/diag.o src/exec.o src/fs.o src/lexer.o src/parser.o \
		src/runtime.o src/validate.o -larchive -o tests/fs-test
	./tests/fs-test tests/fixtures/execution/filesystem.cbs
	rm -f tests/fs-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/edit-assert-test.c \
		src/ast.o src/archive.o src/diag.o src/exec.o src/fs.o src/lexer.o src/parser.o \
		src/runtime.o src/validate.o -larchive -o tests/edit-assert-test
	./tests/edit-assert-test tests/fixtures/execution/edit-assert.cbs
	rm -f tests/edit-assert-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/runtime-test.c \
		src/ast.o src/archive.o src/diag.o src/exec.o src/fs.o src/lexer.o src/parser.o \
		src/runtime.o src/validate.o -larchive -o tests/runtime-test
	./tests/runtime-test tests/fixtures/execution/failure.cbs
	rm -f tests/runtime-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/identity-test.c \
		src/ast.o src/diag.o src/exec.o src/identity.o src/lexer.o src/parser.o \
		src/validate.o -o tests/identity-test
	./tests/identity-test tests/fixtures/execution/identity.cbs
	rm -f tests/identity-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/source-test.c \
		src/ast.o src/archive.o src/diag.o src/exec.o src/lexer.o src/parser.o src/source.o \
		src/validate.o -larchive -o tests/source-test
	./tests/source-test tests/fixtures/execution/sources.cbs
	rm -f tests/source-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/fetch-test.c \
		src/ast.o src/archive.o src/diag.o src/exec.o src/lexer.o src/parser.o src/source.o \
		src/validate.o -larchive -o tests/fetch-test
	./tests/fetch-test tests/fixtures/execution/sources.cbs
	rm -f tests/fetch-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/archive-test.c src/archive.o src/diag.o \
		-larchive -o tests/archive-test
	./tests/archive-test
	rm -f tests/archive-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/extract-test.c \
		src/ast.o src/archive.o src/diag.o src/exec.o src/fs.o src/lexer.o \
		src/parser.o src/runtime.o src/validate.o -larchive -o tests/extract-test
	./tests/extract-test tests/fixtures/execution/extract.cbs
	rm -f tests/extract-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/jobs-test.c src/ast.o src/diag.o src/exec.o \
		src/archive.o src/fs.o src/lexer.o src/parser.o src/runtime.o src/validate.o \
		-larchive -o tests/jobs-test
	./tests/jobs-test
	rm -f tests/jobs-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/stage-test.c src/runtime.o src/archive.o src/ast.o src/diag.o src/exec.o src/fs.o src/lexer.o src/parser.o src/validate.o -larchive -o tests/stage-test
	./tests/stage-test
	rm -f tests/stage-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/manifest-test.c src/manifest.o src/source.o src/archive.o \
		src/ast.o src/diag.o src/exec.o src/lexer.o src/parser.o src/validate.o -larchive -o tests/manifest-test
	./tests/manifest-test
	rm -f tests/manifest-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/package-test.c src/package.o src/cixpkg.o src/source.o src/manifest.o src/archive.o \
		src/ast.o src/diag.o src/exec.o src/fs.o src/lexer.o src/parser.o src/validate.o src/plan.o src/workspace.o src/identity.o src/runtime.o \
		-larchive -lzstd -o tests/package-test
	./tests/package-test
	rm -f tests/package-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/repro-test.c src/package.o src/cixpkg.o src/source.o src/manifest.o src/archive.o \
		src/ast.o src/diag.o src/exec.o src/fs.o src/lexer.o src/parser.o src/validate.o src/plan.o src/workspace.o src/identity.o src/runtime.o \
		-larchive -lzstd -o tests/repro-test
	./tests/repro-test
	rm -f tests/repro-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/dependency-test.c \
		src/ast.o src/dependency.o src/diag.o src/exec.o src/lexer.o src/parser.o \
		src/validate.o -o tests/dependency-test
	./tests/dependency-test tests/fixtures/valid/complete.cbs
	rm -f tests/dependency-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/plan-test.c \
		$(filter-out src/main.o,$(OBJECTS)) -larchive -lzstd -ldl -o tests/plan-test
	./tests/plan-test
	rm -f tests/plan-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/observe-test.c src/observe.o -o tests/observe-test
	./tests/observe-test
	rm -f tests/observe-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/fuzz-test.c \
		$(filter-out src/main.o,$(OBJECTS)) -larchive -lzstd -ldl -o tests/fuzz-test
	./tests/fuzz-test
	rm -f tests/fuzz-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/seams-test.c src/api.o src/observe.o src/sandbox.o src/service.o src/signature.o -o tests/seams-test
	./tests/seams-test
	rm -f tests/seams-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/typed-package-test.c \
		$(filter-out src/main.o,$(OBJECTS)) -larchive -lzstd -ldl -o tests/typed-package-test
	./tests/typed-package-test
	rm -f tests/typed-package-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/policy-test.c src/package.o src/cixpkg.o src/source.o src/manifest.o \
		src/archive.o src/ast.o src/diag.o src/exec.o src/fs.o src/lexer.o src/parser.o src/validate.o \
		src/plan.o src/workspace.o src/identity.o src/runtime.o -larchive -lzstd -o tests/policy-test
	./tests/policy-test
	rm -f tests/policy-test

upstream-test: $(TARGET)
	./tests/upstream-smoke-test.sh ./$(TARGET)

qualification-test: test upstream-test

clean:
	rm -f $(OBJECTS) $(TARGET) $(LIBRARY) tests/exec-test tests/fs-test \
		tests/edit-assert-test tests/http-server
	rm -f tests/runtime-test
	rm -f tests/identity-test
	rm -f tests/source-test
	rm -f tests/fetch-test
	rm -f tests/archive-test
	rm -f tests/extract-test
	rm -f tests/jobs-test
	rm -f tests/stage-test
	rm -f tests/manifest-test
	rm -f tests/package-test
	rm -f tests/repro-test
	rm -f tests/dependency-test
	rm -f tests/plan-test
	rm -f tests/observe-test
	rm -f tests/fuzz-test
	rm -f tests/seams-test
	rm -f tests/typed-package-test
