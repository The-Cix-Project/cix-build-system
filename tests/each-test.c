#define _POSIX_C_SOURCE 200809L
/* Regression tests for parse-time each expansion: bound items in every
 * string field, nesting, per-item environment scope, and item-naming
 * failures. */
#include "cbs.h"

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

static int regular_with(const char *path, const char *expected) {
    struct stat status;
    char content[128];
    FILE *file;
    size_t length = strlen(expected);

    if (lstat(path, &status) != 0 || !S_ISREG(status.st_mode))
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

/* Run a block with stderr captured; return the captured text. */
static int run_captured(const CbsNode *block, const CbsExecutionContext *context,
                        char *output, size_t size) {
    FILE *capture = tmpfile();
    int saved = dup(STDERR_FILENO);
    int result;
    size_t length;

    if (capture == NULL || saved < 0 ||
        dup2(fileno(capture), STDERR_FILENO) < 0)
        return -1;
    result = cbs_execute_block(block, context);
    fflush(stderr);
    dup2(saved, STDERR_FILENO);
    close(saved);
    rewind(capture);
    length = fread(output, 1, size - 1, capture);
    output[length] = '\0';
    fclose(capture);
    return result;
}

static int run_test(const char *recipe_path) {
    char template[] = "/tmp/cbs-each-test-XXXXXX";
    char *base = mkdtemp(template);
    char src[512], build[512], dest[512], path[512];
    char output[4096];
    char *source;
    size_t source_length;
    CbsTokenList tokens;
    CbsNode *document = NULL;
    CbsNode *build_phase;
    CbsNode *check_phase;
    CbsExecutionContext context;
    const char *error;
    const char *on_fail_note;
    const char *note;
    struct stat status;
    int result = 1;

    memset(&tokens, 0, sizeof(tokens));
    if (base == NULL || !path_join(src, sizeof(src), base, "src") ||
        !path_join(build, sizeof(build), base, "build") ||
        !path_join(dest, sizeof(dest), base, "dest") ||
        mkdir(src, 0755) != 0 || mkdir(build, 0755) != 0 ||
        mkdir(dest, 0755) != 0)
        return 1;
    source = read_file(recipe_path, &source_length);
    if (source == NULL || !cbs_lex(recipe_path, source, source_length, &tokens))
        return 1;
    document = cbs_parse(recipe_path, source, source_length, &tokens);
    if (document == NULL || !cbs_validate(document, recipe_path, source))
        goto cleanup;
    build_phase = find_phase(document, "build");
    check_phase = find_phase(document, "check");
    if (build_phase == NULL || check_phase == NULL)
        goto cleanup;
    memset(&context, 0, sizeof(context));
    context.recipe_path = recipe_path;
    context.recipe_source = source;
    context.name = "each-test";
    context.version = "1";
    context.release = 1;
    context.arch = "x86_64";
    context.src = src;
    context.build = build;
    context.dest = dest;
    context.jobs = 1;
    context.working_directory = build;

    /* Every item runs the whole body; the bound name substitutes in paths,
     * written text, copy destinations, and require targets. */
    if (!cbs_execute_block(build_phase, &context))
        goto cleanup;
    path_join(path, sizeof(path), dest, "usr/lib/libcap.so.2");
    if (!regular_with(path, "payload for libcap.so.2\n"))
        goto cleanup;
    path_join(path, sizeof(path), dest, "usr/lib/libresolv.so.2");
    if (!regular_with(path, "payload for libresolv.so.2\n"))
        goto cleanup;
    /* Nested each: both names bind independently. */
    path_join(path, sizeof(path), build, "O2-beta");
    if (!regular_with(path, "O2/beta\n"))
        goto cleanup;
    path_join(path, sizeof(path), build, "O0-alpha");
    if (!regular_with(path, "O0/alpha\n"))
        goto cleanup;
    /* each inside cd. */
    path_join(path, sizeof(path), build, "cd-two");
    if (!regular_with(path, "two"))
        goto cleanup;

    /* The second item fails: its own diagnostic comes first, the note names
     * the item and ordinal, and the third item never runs. */
    if (run_captured(check_phase, &context, output, sizeof(output)) != 0)
        goto cleanup;
    error = strstr(output, "error[CPDL-E4005]");
    on_fail_note = strstr(output, "note[CPDL-N4001]");
    note = strstr(output, "note[CPDL-N4002]");
    /* The body's on_fail ran for the failing item (its `run "false"` fails
     * and is reported as a subordinate note) before the item note. */
    if (error == NULL || on_fail_note == NULL || note == NULL ||
        on_fail_note < error || note < on_fail_note ||
        strstr(output, "required file `${build}/expected-missing` does not "
                       "exist") == NULL ||
        strstr(note, "while processing each `item` item 2 (`missing`)") ==
            NULL)
        goto cleanup;
    path_join(path, sizeof(path), build, "seen-present");
    if (lstat(path, &status) != 0)
        goto cleanup;
    path_join(path, sizeof(path), build, "seen-missing");
    if (lstat(path, &status) != 0)
        goto cleanup;
    path_join(path, sizeof(path), build, "seen-never");
    if (lstat(path, &status) == 0)
        goto cleanup;
    result = 0;
cleanup:
    cbs_node_destroy(document);
    cbs_token_list_destroy(&tokens);
    free(source);
    return result;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fputs("usage: each-test RECIPE\n", stderr);
        return 2;
    }
    if (run_test(argv[1]) != 0)
        return 1;
    puts("each expansion tests: PASS (bound items, nesting, scoped env, cd, "
         "on_fail, and item-naming failure)");
    return 0;
}
