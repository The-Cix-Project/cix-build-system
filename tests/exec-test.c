#define _POSIX_C_SOURCE 200809L

#include "cbs.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char *read_file(const char *path, size_t *length)
{
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

static CbsNode *find_phase(CbsNode *document, const char *name)
{
    CbsNode *package = document->children[0];
    size_t index;

    for (index = 0; index < package->child_count; ++index) {
        CbsNode *item = package->children[index];
        if (item->kind == CBS_NODE_PHASE && strcmp(item->name, name) == 0)
            return item;
    }
    return NULL;
}

static int probe_with_environment(int argc, char **argv)
{
    static const char *const expected[] = {
        "--probe-with-env",
        "space value",
        "\"double quotes\"",
        "$",
        "*",
        ";",
        "'single quotes'",
        "back\\slash",
        "",
        "jobs=3"
    };
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
    return 23;
}

static int probe_without_environment(int argc, char **argv)
{
    if (argc != 2 || strcmp(argv[1], "--probe-without-env") != 0)
        return 120;
    if (getenv("CBS_TEST_ENV") != NULL)
        return 121;
    return 0;
}

static int expected_runtime_failure(const CbsNode *run,
                                    const CbsExecutionContext *context,
                                    const char *code)
{
    FILE *capture = tmpfile();
    int saved_stderr;
    int executed;
    char output[4096];
    size_t length;

    if (capture == NULL)
        return 0;
    saved_stderr = dup(STDERR_FILENO);
    if (saved_stderr < 0 || dup2(fileno(capture), STDERR_FILENO) < 0) {
        if (saved_stderr >= 0)
            close(saved_stderr);
        fclose(capture);
        return 0;
    }
    executed = cbs_execute_run(run, context);
    fflush(stderr);
    dup2(saved_stderr, STDERR_FILENO);
    close(saved_stderr);
    rewind(capture);
    length = fread(output, 1, sizeof(output) - 1, capture);
    output[length] = '\0';
    fclose(capture);
    return !executed && strstr(output, code) != NULL;
}

static int run_parent(const char *recipe_path, const char *executable_path)
{
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
    if (phase == NULL || phase->child_count != 5)
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
            if ((size_t)(separator - executable_path) >= sizeof(build_directory))
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
    context.dest = "/tmp";
    context.jobs = 3;
    context.working_directory = current_directory;
    if (getenv("CBS_TEST_ENV") != NULL)
        goto cleanup_document;
    for (index = 0; index < 2; ++index) {
        if (phase->children[index]->kind != CBS_NODE_RUN ||
            !cbs_execute_run(phase->children[index], &context))
            goto cleanup_document;
    }
    if (!expected_runtime_failure(phase->children[2], &context, "CPDL-E4001") ||
        !expected_runtime_failure(phase->children[3], &context, "CPDL-E4003") ||
        !expected_runtime_failure(phase->children[4], &context, "CPDL-E4002"))
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

int main(int argc, char **argv)
{
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
    if (argc != 2) {
        fputs("usage: exec-test RECIPE.cbs\n", stderr);
        return 2;
    }
    if (run_parent(argv[1], argv[0]) != 0) {
        fputs("run execution tests: FAIL\n", stderr);
        return 1;
    }
    puts("run execution tests: PASS (byte-exact argv and local environment)");
    return 0;
}
