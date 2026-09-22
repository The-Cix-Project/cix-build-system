#define _POSIX_C_SOURCE 200809L

/* Regression tests for argv fidelity, limits, signals, and timeouts. */
#include "cbs.h"
#include "temp.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
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

static int probe_with_environment(int argc, char **argv) {
    static const char *const expected[] = {"--probe-with-env",
                                           "space value",
                                           "\"double quotes\"",
                                           "$",
                                           "*",
                                           ";",
                                           "'single quotes'",
                                           "back\\slash",
                                           "",
                                           "jobs=3"};
    const char *environment = getenv("CBS_TEST_ENV");
    size_t index;

    if (argc != 11)
        return 90;
    for (index = 0; index < sizeof(expected) / sizeof(expected[0]); ++index) {
        if (strcmp(argv[index + 1], expected[index]) != 0)
            return 91 + (int)index;
    }
    if (environment == NULL || strcmp(environment, "literal $ * ; \" '") != 0)
        return 110;
    /* A lowercase name (an autoconf cache variable) is exported as-is. */
    environment = getenv("ac_cv_prog_CC");
    if (environment == NULL || strcmp(environment, "tcc") != 0)
        return 111;
    return 23;
}

static int probe_without_environment(int argc, char **argv) {
    if (argc != 2 || strcmp(argv[1], "--probe-without-env") != 0)
        return 120;
    if (getenv("CBS_TEST_ENV") != NULL || getenv("ac_cv_prog_CC") != NULL)
        return 121;
    return 0;
}

/* The test root, at file scope so main can remove it, and the root the
 * stderr capture file is written under. */
static char base[4096];

static int expected_runtime_failure(const CbsNode *run,
                                    const CbsExecutionContext *context,
                                    const char *code) {
    TestCapture capture;
    int executed;
    char output[4096];

    if (!test_capture_begin(&capture, base))
        return 0;
    executed = cbs_execute_run(run, context);
    test_capture_end(&capture, output, sizeof(output));
    return !executed && strstr(output, code) != NULL;
}

static int run_parent(const char *recipe_path, const char *executable_path) {
    char *source;
    size_t source_length;
    CbsTokenList tokens;
    CbsNode *document;
    CbsNode *phase;
    CbsExecutionContext context;
    char current_directory[4096];
    char build_directory[4096];
    size_t index;
    int result = 1;

    memset(&tokens, 0, sizeof(tokens));
    source = read_file(recipe_path, &source_length);
    if (source == NULL)
        return 1;
    if (!cbs_lex(recipe_path, source, source_length, &tokens))
        goto cleanup_source;
    document = cbs_parse(recipe_path, source, source_length, &tokens);
    if (document == NULL)
        goto cleanup_tokens;
    if (!cbs_validate(document, recipe_path, source))
        goto cleanup_document;
    phase = find_phase(document, "build");
    if (phase == NULL || phase->child_count != 7)
        goto cleanup_document;
    if (getcwd(current_directory, sizeof(current_directory)) == NULL)
        goto cleanup_document;
    {
        const char *separator = strrchr(executable_path, '/');
        if (separator == NULL) {
            if (snprintf(build_directory, sizeof(build_directory), "%s",
                         current_directory) >= (int)sizeof(build_directory))
                goto cleanup_document;
        } else if (executable_path[0] == '/') {
            if ((size_t)(separator - executable_path) >=
                sizeof(build_directory))
                goto cleanup_document;
            memcpy(build_directory, executable_path,
                   (size_t)(separator - executable_path));
            build_directory[separator - executable_path] = '\0';
        } else {
            if (snprintf(build_directory, sizeof(build_directory), "%s/%.*s",
                         current_directory, (int)(separator - executable_path),
                         executable_path) >= (int)sizeof(build_directory))
                goto cleanup_document;
        }
    }
    memset(&context, 0, sizeof(context));
    context.recipe_path = recipe_path;
    context.recipe_source = source;
    context.name = "argv-test";
    context.version = "1";
    context.release = 1;
    context.arch = "x86_64";
    context.src = current_directory;
    context.build = build_directory;
    context.dest = base;
    context.jobs = 3;
    context.compiler = "tcc";
    context.working_directory = current_directory;
    context.limits.address_space_mb = 256;
    context.limits.file_size_mb = 1;
    context.limits.cpu_seconds = 2;
    context.limits.open_files = 64;
    context.limits.processes = 64;
    if (getenv("CBS_TEST_ENV") != NULL)
        goto cleanup_document;
    for (index = 0; index < 2; ++index) {
        if (phase->children[index]->kind != CBS_NODE_RUN ||
            !cbs_execute_run(phase->children[index], &context))
            goto cleanup_document;
    }
    if (!expected_runtime_failure(phase->children[2], &context, "CPDL-E4001") ||
        !expected_runtime_failure(phase->children[3], &context, "CPDL-E4003") ||
        !expected_runtime_failure(phase->children[4], &context, "CPDL-E4002") ||
        !cbs_execute_run(phase->children[5], &context) ||
        !cbs_execute_run(phase->children[6], &context))
        goto cleanup_document;
    if (getenv("CBS_TEST_ENV") != NULL)
        goto cleanup_document;
    result = 0;

cleanup_document:
    cbs_node_destroy(document);
cleanup_tokens:
    cbs_token_list_destroy(&tokens);
cleanup_source:
    free(source);
    return result;
}

/* Exercise child execution, limits, timeout, and interrupt behavior. */
int main(int argc, char **argv) {
    if (argc >= 2 && strcmp(argv[1], "--probe-with-env") == 0)
        return probe_with_environment(argc, argv);
    if (argc >= 2 && strcmp(argv[1], "--probe-without-env") == 0)
        return probe_without_environment(argc, argv);
    if (argc >= 2 && strcmp(argv[1], "--probe-fail") == 0)
        return 9;
    if (argc >= 2 && strcmp(argv[1], "--probe-sleep") == 0) {
        sleep(5);
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "--probe-signal") == 0) {
        raise(SIGTERM);
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "--probe-limit") == 0) {
        struct rlimit limit;
        if (getrlimit(RLIMIT_AS, &limit) != 0 ||
            limit.rlim_cur > 256UL * 1024UL * 1024UL)
            return 1;
        if (getrlimit(RLIMIT_FSIZE, &limit) != 0 ||
            limit.rlim_cur > 1024UL * 1024UL)
            return 2;
        if (getrlimit(RLIMIT_NOFILE, &limit) != 0 || limit.rlim_cur > 64)
            return 3;
        if (getrlimit(RLIMIT_NPROC, &limit) != 0 || limit.rlim_cur > 64)
            return 4;
        if (getrlimit(RLIMIT_CPU, &limit) != 0 || limit.rlim_cur > 2)
            return 5;
        return 0;
    }
    if (argc != 2) {
        fputs("usage: exec-test RECIPE.cbs\n", stderr);
        return 2;
    }
    if (!test_temp_root(base, sizeof(base), "cbs-exec-test"))
        return 1;
    if (run_parent(argv[1], argv[0]) != 0) {
        fputs("run execution tests: FAIL\n", stderr);
        test_remove_tree(base);
        return 1;
    }
    test_remove_tree(base);
    puts("run execution tests: PASS (byte-exact argv and local environment)");
    return 0;
}
