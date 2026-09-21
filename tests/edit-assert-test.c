#define _POSIX_C_SOURCE 200809L

/* Regression tests for source edits, cardinality, and atomicity. */
#include "cbs.h"
#include "temp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char *read_source(const char *path, size_t *length) {
    FILE *file = fopen(path, "rb");
    long size;
    char *source;

    if (file == NULL || fseek(file, 0, SEEK_END) != 0 ||
        (size = ftell(file)) < 0 || fseek(file, 0, SEEK_SET) != 0)
        return NULL;
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

static CbsNode *find_phase(CbsNode *document) {
    CbsNode *package = document->children[0];
    size_t index;
    for (index = 0; index < package->child_count; ++index)
        if (package->children[index]->kind == CBS_NODE_PHASE)
            return package->children[index];
    return NULL;
}

static int join(char *output, size_t size, const char *left,
                const char *right) {
    return snprintf(output, size, "%s/%s", left, right) < (int)size;
}

static int file_equals(const char *path, const unsigned char *expected,
                       size_t expected_length, mode_t mode) {
    struct stat status;
    unsigned char buffer[256];
    FILE *file;
    size_t length;

    if (lstat(path, &status) != 0 || !S_ISREG(status.st_mode) ||
        (status.st_mode & 07777) != mode)
        return 0;
    file = fopen(path, "rb");
    if (file == NULL)
        return 0;
    length = fread(buffer, 1, sizeof(buffer), file);
    fclose(file);
    return length == expected_length &&
           memcmp(buffer, expected, expected_length) == 0;
}

/* Run one assertion with stderr captured; it must fail with the code and,
 * when given, the message fragment. */
/* The test root, and the stderr capture file written under it. Both are
 * file scope so main can remove the root after run_test returns. */
static char base[4096];
static const char *capture_root;

static int expected_failure(const CbsNode *operation,
                            const CbsExecutionContext *context,
                            const char *code, const char *fragment) {
    TestCapture capture;
    int executed;
    char output[2048];

    if (!test_capture_begin(&capture, capture_root))
        return 0;
    executed = cbs_execute_edit_assertion(operation, context);
    test_capture_end(&capture, output, sizeof(output));
    return !executed && strstr(output, code) != NULL &&
           (fragment == NULL || strstr(output, fragment) != NULL);
}

static int write_text(const char *path, const char *text) {
    FILE *file = fopen(path, "wb");
    if (file == NULL)
        return 0;
    if (fputs(text, file) < 0) {
        fclose(file);
        return 0;
    }
    return fclose(file) == 0 && chmod(path, 0644) == 0;
}

static int write_binary(const char *path) {
    static const unsigned char bytes[] = {'A', 0, 'o', 'l', 'd', 'Z'};
    FILE *file = fopen(path, "wb");
    if (file == NULL)
        return 0;
    if (fwrite(bytes, 1, sizeof(bytes), file) != sizeof(bytes)) {
        fclose(file);
        return 0;
    }
    return fclose(file) == 0 && chmod(path, 0600) == 0;
}

static int run_test(const char *recipe_path) {
    static const unsigned char edited_binary[] = {'A', 0, 'n', 'e', 'w', 'Z'};
    char src[4160], build[4160], dest[4160], path[4160], outside[4160];
    char *source = NULL;
    size_t source_length = 0;
    CbsTokenList tokens;
    CbsNode *document = NULL;
    CbsNode *phase;
    CbsExecutionContext context;
    CbsNode operation;
    CbsNode target_property;
    CbsNode until_property;
    CbsNode *property_list[1];
    size_t index;
    int result = 1;

    memset(&tokens, 0, sizeof(tokens));
    if (!test_temp_root(base, sizeof(base), "cbs-edit-test"))
        return 1;
    capture_root = base;
    if (!join(src, sizeof(src), base, "src") ||
        !join(build, sizeof(build), base, "build") ||
        !join(dest, sizeof(dest), base, "dest") ||
        !join(outside, sizeof(outside), base, "outside") ||
        mkdir(src, 0755) != 0 || mkdir(build, 0755) != 0 ||
        mkdir(dest, 0755) != 0 || mkdir(outside, 0755) != 0)
        return 1;
    source = read_source(recipe_path, &source_length);
    if (source == NULL || !cbs_lex(recipe_path, source, source_length, &tokens))
        goto cleanup;
    document = cbs_parse(recipe_path, source, source_length, &tokens);
    if (document == NULL || !cbs_validate(document, recipe_path, source))
        goto cleanup;
    phase = find_phase(document);
    if (phase == NULL)
        goto cleanup;
    memset(&context, 0, sizeof(context));
    context.recipe_path = recipe_path;
    context.recipe_source = source;
    context.name = "edit-assert-test";
    context.version = "1";
    context.release = 1;
    context.arch = "x86_64";
    context.src = src;
    context.build = build;
    context.dest = dest;
    context.jobs = 1;
    context.working_directory = src;
    for (index = 0; index < phase->child_count; ++index) {
        CbsNode *item = phase->children[index];
        int success =
            item->kind == CBS_NODE_MKDIR || item->kind == CBS_NODE_WRITE ||
                    item->kind == CBS_NODE_SYMLINK
                ? cbs_execute_filesystem(item, &context)
                : cbs_execute_edit_assertion(item, &context);
        if (!success)
            goto cleanup;
    }
    join(path, sizeof(path), build, "input.bin");
    if (!file_equals(path, (const unsigned char *)"new! middle new!", 16, 0640))
        goto cleanup;
    join(path, sizeof(path), build, "truncate.txt");
    if (!file_equals(path, (const unsigned char *)"keep\n", 5, 0644))
        goto cleanup;
    join(path, sizeof(path), build, "input.bin");

    memset(&operation, 0, sizeof(operation));
    operation.location.path = recipe_path;
    operation.location.line = 1;
    operation.location.column = 1;
    operation.kind = CBS_NODE_REPLACE;
    operation.name = "${build}/input.bin";
    operation.value = "new!";
    operation.second_value = "wrong";
    operation.flag = CBS_TOKEN_STRING;
    operation.second_flag = CBS_TOKEN_STRING;
    operation.number = 1;
    if (!expected_failure(&operation, &context, "CPDL-E4005", NULL) ||
        !file_equals(path, (const unsigned char *)"new! middle new!", 16, 0640))
        goto cleanup;
    operation.number = 3;
    if (!expected_failure(&operation, &context, "CPDL-E4005", NULL) ||
        !file_equals(path, (const unsigned char *)"new! middle new!", 16, 0640))
        goto cleanup;

    join(path, sizeof(path), build, "binary");
    if (!write_binary(path))
        goto cleanup;
    operation.name = "${build}/binary";
    operation.value = "old";
    operation.second_value = "new";
    operation.number = 1;
    if (!cbs_execute_edit_assertion(&operation, &context) ||
        !file_equals(path, edited_binary, sizeof(edited_binary), 0600))
        goto cleanup;

    operation.kind = CBS_NODE_REQUIRE;
    operation.name = "glob";
    operation.value = "${build}/matches/*";
    operation.number = 1;
    operation.flag = 0;
    if (!expected_failure(&operation, &context, "CPDL-E4005", NULL))
        goto cleanup;

    /* A failed require says what the path is, and "does not exist" is
     * reserved for an absent path (#175). */
    operation.number = 0;
    operation.name = "file";
    operation.value = "${build}/input-link";
    if (!expected_failure(&operation, &context, "CPDL-E4005",
                          "required file `${build}/input-link` is a symbolic "
                          "link; require file matches regular files only"))
        goto cleanup;
    operation.value = "${build}/matches";
    if (!expected_failure(&operation, &context, "CPDL-E4005",
                          "is a directory; require file matches regular "
                          "files only"))
        goto cleanup;
    operation.value = "${build}/absent";
    if (!expected_failure(&operation, &context, "CPDL-E4005",
                          "required file `${build}/absent` does not exist"))
        goto cleanup;
    operation.value = "${build}/input.bin/child";
    if (!expected_failure(&operation, &context, "CPDL-E4005",
                          "has a parent that is a symbolic link or not a "
                          "directory"))
        goto cleanup;
    operation.name = "directory";
    operation.value = "${build}/input-link";
    if (!expected_failure(&operation, &context, "CPDL-E4005",
                          "is a symbolic link; require directory matches "
                          "directories only"))
        goto cleanup;
    operation.value = "${build}/input.bin";
    if (!expected_failure(&operation, &context, "CPDL-E4005",
                          "is a regular file; require directory matches "
                          "directories only"))
        goto cleanup;
    operation.name = "symlink";
    if (!expected_failure(&operation, &context, "CPDL-E4005",
                          "is a regular file; require symlink matches "
                          "symbolic links only"))
        goto cleanup;
    operation.value = "${build}/absent";
    if (!expected_failure(&operation, &context, "CPDL-E4005",
                          "required symlink `${build}/absent` does not exist"))
        goto cleanup;
    memset(&target_property, 0, sizeof(target_property));
    target_property.kind = CBS_NODE_PROPERTY;
    target_property.name = "target";
    target_property.value = "wrong";
    target_property.flag = CBS_TOKEN_STRING;
    property_list[0] = &target_property;
    operation.children = property_list;
    operation.child_count = 1;
    operation.value = "${build}/input-link";
    if (!expected_failure(&operation, &context, "CPDL-E4005",
                          "points to `input.bin`, expected `wrong`"))
        goto cleanup;
    target_property.value = "input.bin";
    if (!cbs_execute_edit_assertion(&operation, &context))
        goto cleanup;
    /* A dangling link is still a link; the target is compared, not followed. */
    target_property.value = "missing";
    operation.value = "${build}/dangling";
    if (!cbs_execute_edit_assertion(&operation, &context))
        goto cleanup;
    operation.children = NULL;
    operation.child_count = 0;

    /* until whitespace: the match runs to end of file when no delimiter
     * follows, and an unexpected count still fails before mutation (#177). */
    join(path, sizeof(path), build, "eof.mk");
    if (!write_text(path, "X=-Wl,--version-script=a.map"))
        goto cleanup;
    memset(&until_property, 0, sizeof(until_property));
    until_property.kind = CBS_NODE_PROPERTY;
    until_property.name = "until";
    until_property.value = "whitespace";
    property_list[0] = &until_property;
    operation.kind = CBS_NODE_REPLACE;
    operation.name = "${build}/eof.mk";
    operation.value = "-Wl,--version-script=";
    operation.second_value = "";
    operation.flag = CBS_TOKEN_STRING;
    operation.second_flag = CBS_TOKEN_STRING;
    operation.children = property_list;
    operation.child_count = 1;
    operation.number = 2;
    if (!expected_failure(&operation, &context, "CPDL-E4005",
                          "source edit expected 2 matches but found 1") ||
        !file_equals(path, (const unsigned char *)"X=-Wl,--version-script=a.map",
                     28, 0644))
        goto cleanup;
    operation.number = 1;
    if (!cbs_execute_edit_assertion(&operation, &context) ||
        !file_equals(path, (const unsigned char *)"X=", 2, 0644))
        goto cleanup;
    operation.children = NULL;
    operation.child_count = 0;

    operation.kind = CBS_NODE_REPLACE;
    operation.name = "${dest}/../outside/escaped";
    operation.value = "x";
    operation.second_value = "y";
    operation.number = 1;
    operation.flag = CBS_TOKEN_STRING;
    if (!expected_failure(&operation, &context, "CPDL-E4004", NULL))
        goto cleanup;
    result = 0;

cleanup:
    cbs_node_destroy(document);
    cbs_token_list_destroy(&tokens);
    free(source);
    return result;
}

/* Exercise source-edit cardinality, atomicity, and assertion failures. */
int main(int argc, char **argv) {
    if (argc != 2) {
        fputs("usage: edit-assert-test RECIPE.cbs\n", stderr);
        return 2;
    }
    if (run_test(argv[1]) != 0) {
        fputs("source edit and assertion tests: FAIL\n", stderr);
        test_remove_tree(base);
        return 1;
    }
    test_remove_tree(base);
    puts("source edit and assertion tests: PASS (cardinality, atomicity, and "
         "assertion diagnostics)");
    return 0;
}
