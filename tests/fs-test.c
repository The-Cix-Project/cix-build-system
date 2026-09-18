#define _POSIX_C_SOURCE 200809L

/* Regression tests for confined filesystem operations and glob handling. */
#include "cbs.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char *read_file(const char *path, size_t *length) {
    FILE *file = fopen(path, "rb");
    long size;
    char *source;

    if (file == NULL || fseek(file, 0, SEEK_END) != 0 ||
        (size = ftell(file)) < 0 || fseek(file, 0, SEEK_SET) != 0) {
        if (file != NULL)
            fclose(file);
        return NULL;
    }
    source = cbs_allocate((size_t)size + 1);
    if (fread(source, 1, (size_t)size, file) != (size_t)size) {
        fclose(file);
        free(source);
        return NULL;
    }
    fclose(file);
    source[size] = '\0';
    *length = (size_t)size;
    return source;
}

static CbsNode *find_phase(CbsNode *document, const char *name) {
    CbsNode *package = document->children[0];
    size_t index;

    for (index = 0; index < package->child_count; ++index) {
        CbsNode *item = package->children[index];
        if (item->kind == CBS_NODE_PHASE && strcmp(item->name, name) == 0)
            return item;
    }
    return NULL;
}

static int path_join(char *buffer, size_t size, const char *left,
                     const char *right) {
    return snprintf(buffer, size, "%s/%s", left, right) < (int)size;
}

static int regular_with(const char *path, const char *expected, mode_t mode) {
    struct stat status;
    char content[64];
    FILE *file;
    size_t length = strlen(expected);

    if (lstat(path, &status) != 0 || !S_ISREG(status.st_mode) ||
        (status.st_mode & 07777) != mode)
        return 0;
    file = fopen(path, "rb");
    if (file == NULL || fread(content, 1, length + 1, file) != length) {
        if (file != NULL)
            fclose(file);
        return 0;
    }
    fclose(file);
    return memcmp(content, expected, length) == 0;
}

static int expect_failure(const CbsNode *operation,
                          const CbsExecutionContext *context,
                          const char *fragment) {
    FILE *capture = tmpfile();
    int saved = dup(STDERR_FILENO);
    int result;
    char output[2048];
    size_t length;

    if (capture == NULL || saved < 0 ||
        dup2(fileno(capture), STDERR_FILENO) < 0)
        return 0;
    result = cbs_execute_filesystem(operation, context);
    fflush(stderr);
    dup2(saved, STDERR_FILENO);
    close(saved);
    rewind(capture);
    length = fread(output, 1, sizeof(output) - 1, capture);
    output[length] = '\0';
    fclose(capture);
    return !result && strstr(output, "CPDL-E4004") != NULL &&
           (fragment == NULL || strstr(output, fragment) != NULL);
}

static int expect_edit_failure(const CbsNode *operation,
                               const CbsExecutionContext *context) {
    return !cbs_execute_edit_assertion(operation, context);
}

static int run_test(const char *recipe_path) {
    char template[] = "/tmp/cbs-fs-test-XXXXXX";
    char *base = mkdtemp(template);
    char src[512], build[512], dest[512], outside[512], path[512], target[64];
    char *source;
    size_t source_length;
    CbsTokenList tokens;
    CbsNode *document = NULL;
    CbsNode *phase;
    CbsExecutionContext context;
    CbsNamedSource named_source;
    CbsNode escape;
    struct stat status;
    ssize_t target_length;
    size_t index;
    int result = 1;

    memset(&tokens, 0, sizeof(tokens));
    if (base == NULL || !path_join(src, sizeof(src), base, "src") ||
        !path_join(build, sizeof(build), base, "build") ||
        !path_join(dest, sizeof(dest), base, "dest") ||
        !path_join(outside, sizeof(outside), base, "outside") ||
        mkdir(src, 0755) != 0 || mkdir(build, 0755) != 0 ||
        mkdir(dest, 0755) != 0 || mkdir(outside, 0755) != 0)
        return 1;
    source = read_file(recipe_path, &source_length);
    if (source == NULL || !cbs_lex(recipe_path, source, source_length, &tokens))
        return 1;
    document = cbs_parse(recipe_path, source, source_length, &tokens);
    if (document == NULL || !cbs_validate(document, recipe_path, source))
        goto cleanup;
    phase = find_phase(document, "build");
    if (phase == NULL)
        goto cleanup;
    memset(&context, 0, sizeof(context));
    context.recipe_path = recipe_path;
    context.recipe_source = source;
    context.name = "filesystem-test";
    context.version = "1";
    context.release = 1;
    context.arch = "x86_64";
    context.src = src;
    context.build = build;
    context.dest = dest;
    context.jobs = 1;
    context.working_directory = src;
    path_join(path, sizeof(path), src, "payload");
    named_source.name = "payload";
    named_source.path = path;
    context.sources = &named_source;
    context.source_count = 1;
    for (index = 0; index < phase->child_count; ++index) {
        CbsNode *operation = phase->children[index];
        int operation_result;
        if (operation->kind == CBS_NODE_REPLACE ||
            operation->kind == CBS_NODE_INSERT)
            operation_result = cbs_execute_edit_assertion(operation, &context);
        else
            operation_result = cbs_execute_filesystem(operation, &context);
        if (!operation_result)
            goto cleanup;
    }

    path_join(path, sizeof(path), build, "input");
    if (lstat(path, &status) != 0 || (status.st_mode & 07777) != 0700)
        goto cleanup;
    path_join(path, sizeof(path), dest, "copied/a.txt");
    if (!regular_with(path, "alpha", 0644))
        goto cleanup;
    path_join(path, sizeof(path), dest, "moved.txt");
    if (!regular_with(path, "beta", 0644))
        goto cleanup;
    path_join(path, sizeof(path), build, "replace.txt");
    if (!regular_with(path, "new", 0640))
        goto cleanup;
    path_join(path, sizeof(path), dest, "named-source");
    if (!regular_with(path, "named source", 0644))
        goto cleanup;
    path_join(path, sizeof(path), dest, "copied/a-link");
    target_length = readlink(path, target, sizeof(target) - 1);
    if (target_length != 5)
        goto cleanup;
    target[target_length] = '\0';
    if (strcmp(target, "a.txt") != 0)
        goto cleanup;
    path_join(path, sizeof(path), build, "input/drop.tmp");
    if (lstat(path, &status) == 0 || errno != ENOENT)
        goto cleanup;
    path_join(path, sizeof(path), build, "discard");
    if (lstat(path, &status) == 0 || errno != ENOENT)
        goto cleanup;
    path_join(path, sizeof(path), build, "patterns/a.txt");
    if (!regular_with(path, "a", 0640))
        goto cleanup;
    path_join(path, sizeof(path), build, "patterns/b.txt");
    if (!regular_with(path, "b", 0640))
        goto cleanup;
    path_join(path, sizeof(path), build, "patterns/c.txt");
    if (!regular_with(path, "c", 0641))
        goto cleanup;
    path_join(path, sizeof(path), build, "patterns/q.x");
    if (!regular_with(path, "q", 0642))
        goto cleanup;
    path_join(path, sizeof(path), build, "patterns/literal*.txt");
    if (!regular_with(path, "star", 0644))
        goto cleanup;
    path_join(path, sizeof(path), build, "bulk/a.txt");
    if (!regular_with(path, "new", 0644))
        goto cleanup;
    path_join(path, sizeof(path), build, "bulk/z.txt");
    if (!regular_with(path, "new", 0644))
        goto cleanup;
    path_join(path, sizeof(path), build, "terminal");
    if (lstat(path, &status) == 0 || errno != ENOENT)
        goto cleanup;

    memset(&escape, 0, sizeof(escape));
    escape.kind = CBS_NODE_WRITE;
    escape.location.path = recipe_path;
    escape.location.line = 1;
    escape.location.column = 1;
    escape.value = "${dest}/../outside/escaped";
    escape.second_value = "bad";
    escape.flag = CBS_TOKEN_STRING;
    if (!expect_failure(&escape, &context, NULL))
        goto cleanup;

    /* stage library: the first sandbox copy of libc.so.6 is shipped with its
     * mode, and the destination directory is created on demand. The test
     * assumes a glibc host, as the manual already does. */
    {
        static const char *const candidates[] = {
            "/usr/lib/x86_64-linux-gnu/libc.so.6",
            "/lib/x86_64-linux-gnu/libc.so.6", "/usr/lib/libc.so.6",
            "/lib/libc.so.6", "/usr/lib64/libc.so.6", "/lib64/libc.so.6"};
        struct stat host;
        CbsNode missing;
        size_t candidate;
        int have_host = 0;
        for (candidate = 0; candidate < 6 && !have_host; ++candidate)
            have_host = lstat(candidates[candidate], &host) == 0;
        if (!have_host)
            goto cleanup;
        path_join(path, sizeof(path), dest, "usr/lib");
        if (lstat(path, &status) != 0 || !S_ISDIR(status.st_mode))
            goto cleanup;
        path_join(path, sizeof(path), dest, "usr/lib/libc.so.6");
        if (lstat(path, &status) != 0 ||
            S_ISREG(status.st_mode) != S_ISREG(host.st_mode) ||
            S_ISLNK(status.st_mode) != S_ISLNK(host.st_mode) ||
            (status.st_mode & 07777) != (host.st_mode & 07777) ||
            (S_ISREG(host.st_mode) && status.st_size != host.st_size))
            goto cleanup;
        memset(&missing, 0, sizeof(missing));
        missing.kind = CBS_NODE_STAGE;
        missing.location.path = recipe_path;
        missing.location.line = 1;
        missing.location.column = 1;
        missing.value = "libcbs-not-here.so.9";
        missing.second_value = "${dest}/usr/lib";
        if (!expect_failure(&missing, &context,
                            "library is not in the build sandbox (searched "))
            goto cleanup;
        missing.value = "../etc/passwd";
        if (!expect_failure(&missing, &context,
                            "stage library name must be a bare file name"))
            goto cleanup;
    }

    {
        CbsNode mismatch;
        memset(&mismatch, 0, sizeof(mismatch));
        mismatch.kind = CBS_NODE_REPLACE;
        mismatch.location.path = recipe_path;
        mismatch.location.line = 1;
        mismatch.location.column = 1;
        mismatch.selector_glob = 1;
        mismatch.name = "${build}/bulk/*.txt";
        mismatch.value = "old";
        mismatch.flag = CBS_TOKEN_STRING;
        mismatch.second_value = "bad";
        mismatch.second_flag = CBS_TOKEN_STRING;
        mismatch.number = 1;
        if (!expect_edit_failure(&mismatch, &context)) {
            goto cleanup;
        }
    }

    {
        CbsNode empty;
        memset(&empty, 0, sizeof(empty));
        empty.kind = CBS_NODE_REPLACE;
        empty.location.path = recipe_path;
        empty.location.line = 1;
        empty.location.column = 1;
        empty.selector_glob = 1;
        empty.name = "${build}/bulk/*.missing";
        empty.value = "old";
        empty.flag = CBS_TOKEN_STRING;
        empty.second_value = "bad";
        empty.second_flag = CBS_TOKEN_STRING;
        empty.number = 0;
        if (!expect_edit_failure(&empty, &context))
            goto cleanup;
    }
    path_join(path, sizeof(path), outside, "escaped");
    if (lstat(path, &status) == 0)
        goto cleanup;

    path_join(path, sizeof(path), dest, "linked-parent");
    if (symlink(outside, path) != 0)
        goto cleanup;
    escape.value = "${dest}/linked-parent/escaped";
    if (!expect_failure(&escape, &context, NULL))
        goto cleanup;
    path_join(path, sizeof(path), outside, "escaped");
    if (lstat(path, &status) == 0)
        goto cleanup;

    memset(&escape, 0, sizeof(escape));
    escape.location.path = recipe_path;
    escape.location.line = 1;
    escape.location.column = 1;
    escape.kind = CBS_NODE_COPY;
    escape.value = "${build}/input";
    escape.second_value = "${dest}/directory-copy";
    if (!expect_failure(&escape, &context, NULL))
        goto cleanup;

    escape.kind = CBS_NODE_REMOVE;
    escape.value = "${build}/input";
    escape.second_value = NULL;
    if (!expect_failure(&escape, &context, NULL))
        goto cleanup;

    escape.value = "${build}/input/*.missing";
    escape.flag = 1;
    if (!expect_failure(&escape, &context, NULL))
        goto cleanup;

    escape.kind = CBS_NODE_COPY;
    escape.value = "${build}/input/*.txt";
    escape.second_value = "${dest}/moved.txt";
    if (!expect_failure(&escape, &context, NULL))
        goto cleanup;

    escape.kind = CBS_NODE_CHMOD;
    escape.value = "${dest}/copied/a-link";
    escape.second_value = "0777";
    escape.flag = 0;
    if (!expect_failure(&escape, &context, NULL))
        goto cleanup;
    path_join(path, sizeof(path), dest, "copied/a.txt");
    if (!regular_with(path, "alpha", 0644))
        goto cleanup;

    escape.kind = CBS_NODE_SYMLINK;
    escape.value = "target";
    escape.second_value = "${dest}/copied/a-link";
    if (!expect_failure(&escape, &context, NULL))
        goto cleanup;
    result = 0;

cleanup:
    cbs_node_destroy(document);
    cbs_token_list_destroy(&tokens);
    free(source);
    return result;
}

/* Exercise every supported confined filesystem operation. */
int main(int argc, char **argv) {
    if (argc != 2) {
        fputs("usage: fs-test RECIPE.cbs\n", stderr);
        return 2;
    }
    if (run_test(argv[1]) != 0) {
        fputs("filesystem execution tests: FAIL\n", stderr);
        return 1;
    }
    puts("filesystem execution tests: PASS (operations, globs, confinement, "
         "and staged sandbox libraries)");
    return 0;
}
