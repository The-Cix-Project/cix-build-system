#define _POSIX_C_SOURCE 200809L

/* Regression tests for failure attribution and diagnostic notes. */
#include "cbs.h"
#include "temp.h"

#include <fcntl.h>
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

static int copy_executable(const char *source, const char *destination) {
    int input = open(source, O_RDONLY);
    int output;
    char buffer[32768];
    ssize_t length;
    if (input < 0)
        return 0;
    output = open(destination, O_WRONLY | O_CREAT | O_TRUNC, 0700);
    if (output < 0) {
        close(input);
        return 0;
    }
    while ((length = read(input, buffer, sizeof(buffer))) > 0) {
        ssize_t offset = 0;
        while (offset < length) {
            ssize_t written =
                write(output, buffer + offset, (size_t)(length - offset));
            if (written < 0) {
                close(input);
                close(output);
                return 0;
            }
            offset += written;
        }
    }
    close(input);
    return close(output) == 0 && length == 0 && chmod(destination, 0700) == 0;
}

static CbsNode *phase_named(CbsNode *document, const char *name) {
    CbsNode *package = document->children[0];
    size_t index;
    for (index = 0; index < package->child_count; ++index) {
        CbsNode *item = package->children[index];
        if (item->kind == CBS_NODE_PHASE && strcmp(item->name, name) == 0)
            return item;
    }
    return NULL;
}

static int exists(const char *path) {
    struct stat status;
    return lstat(path, &status) == 0;
}

static int probe(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--succeed") == 0)
        return 0;
    if (argc == 3 && strcmp(argv[1], "--fail") == 0)
        return atoi(argv[2]);
    if (argc == 4 && strcmp(argv[1], "--record") == 0) {
        const char *value = getenv("BLOCK_ENV");
        int descriptor;
        if (value == NULL || strcmp(value, argv[3]) != 0)
            return 91;
        descriptor = open(argv[2], O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (descriptor < 0)
            return 92;
        return close(descriptor) == 0 ? 0 : 93;
    }
    return -1;
}

/* The test root, at file scope so main can remove it after run_parent. */
static char base[4096];

static int run_parent(const char *recipe_path, const char *self) {
    char src[4160], build[4160], dest[4160], executable[4160];
    char continued[4160], stopped[4160], success_diagnostic[4160];
    char *source = NULL;
    size_t source_length = 0;
    CbsTokenList tokens;
    CbsNode *document = NULL;
    CbsNode *build_phase;
    CbsNode *check_phase;
    CbsExecutionContext context;
    TestCapture capture;
    int captured = 0;
    char diagnostics[8192];
    const char *primary;
    const char *allowed_note;
    const char *stopping_note;
    int result = 1;

    memset(&tokens, 0, sizeof(tokens));
    if (!test_temp_root(base, sizeof(base), "cbs-runtime-test"))
        return 1;
    if (snprintf(src, sizeof(src), "%s/src", base) >= (int)sizeof(src) ||
        snprintf(build, sizeof(build), "%s/build", base) >=
            (int)sizeof(build) ||
        snprintf(dest, sizeof(dest), "%s/dest", base) >= (int)sizeof(dest) ||
        snprintf(executable, sizeof(executable), "%s/runtime-test", build) >=
            (int)sizeof(executable) ||
        mkdir(src, 0755) != 0 || mkdir(build, 0755) != 0 ||
        mkdir(dest, 0755) != 0 || !copy_executable(self, executable))
        return 1;
    snprintf(continued, sizeof(continued), "%s/continued", build);
    snprintf(stopped, sizeof(stopped), "%s/must-not-run", build);
    snprintf(success_diagnostic, sizeof(success_diagnostic),
             "%s/success-on-fail-ran", build);
    source = read_source(recipe_path, &source_length);
    if (source == NULL || !cbs_lex(recipe_path, source, source_length, &tokens))
        goto cleanup;
    document = cbs_parse(recipe_path, source, source_length, &tokens);
    if (document == NULL || !cbs_validate(document, recipe_path, source))
        goto cleanup;
    build_phase = phase_named(document, "build");
    check_phase = phase_named(document, "check");
    if (build_phase == NULL || check_phase == NULL)
        goto cleanup;
    memset(&context, 0, sizeof(context));
    context.recipe_path = recipe_path;
    context.recipe_source = source;
    context.name = "failure-test";
    context.version = "1";
    context.release = 1;
    context.arch = "x86_64";
    context.src = src;
    context.build = build;
    context.dest = dest;
    context.jobs = 1;
    context.working_directory = src;
    if (!test_capture_begin(&capture, base))
        goto cleanup;
    captured = 1;
    if (cbs_execute_block(build_phase, &context))
        goto restore;
    test_capture_end(&capture, diagnostics, sizeof(diagnostics));
    captured = 0;
    primary = strstr(diagnostics, "error[CPDL-E4001]");
    allowed_note = strstr(diagnostics, "note[CPDL-N4001]");
    stopping_note = allowed_note == NULL
                        ? NULL
                        : strstr(allowed_note + 1, "note[CPDL-N4001]");
    if (primary == NULL || allowed_note == NULL || stopping_note == NULL ||
        !(primary < allowed_note && allowed_note < stopping_note) ||
        strstr(primary, "status 17") == NULL ||
        strstr(allowed_note, "status 18") == NULL ||
        strstr(stopping_note, "status 19") == NULL || !exists(continued) ||
        exists(stopped)) {
        fputs(diagnostics, stderr);
        goto cleanup;
    }
    if (!cbs_execute_block(check_phase, &context) || exists(success_diagnostic))
        goto cleanup;
    result = 0;
    goto cleanup;

restore:
cleanup:
    if (captured)
        test_capture_end(&capture, NULL, 0);
    cbs_node_destroy(document);
    cbs_token_list_destroy(&tokens);
    free(source);
    return result;
}

/* Verify primary runtime failures and subordinate diagnostic notes. */
int main(int argc, char **argv) {
    int probe_result = probe(argc, argv);
    if (probe_result >= 0)
        return probe_result;
    if (argc != 2) {
        fputs("usage: runtime-test RECIPE.cbs\n", stderr);
        return 2;
    }
    if (run_parent(argv[1], argv[0]) != 0) {
        fputs("failure orchestration tests: FAIL\n", stderr);
        test_remove_tree(base);
        return 1;
    }
    test_remove_tree(base);
    puts("failure orchestration tests: PASS (primary cause and subordinate "
         "notes)");
    return 0;
}
