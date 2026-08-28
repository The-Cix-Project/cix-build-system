#define _POSIX_C_SOURCE 200809L

#include "cbs.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    CbsEnvironmentBinding *items;
    size_t count;
    size_t capacity;
    size_t inherited_count;
} EnvironmentList;

static int execute_operation(const CbsNode *operation,
                             const CbsExecutionContext *context);

static int is_filesystem(CbsNodeKind kind)
{
    return kind >= CBS_NODE_MKDIR && kind <= CBS_NODE_CHMOD;
}

static int run_allows_failure(const CbsNode *run)
{
    size_t index;
    for (index = 0; index < run->child_count; ++index)
        if (run->children[index]->kind == CBS_NODE_ALLOW_FAILURE)
            return 1;
    return 0;
}

static char *capture_stream(FILE *stream)
{
    long size;
    char *content;

    fflush(stream);
    if (fseek(stream, 0, SEEK_END) != 0 || (size = ftell(stream)) < 0 ||
        fseek(stream, 0, SEEK_SET) != 0)
        return cbs_duplicate("runtime operation failed without a diagnostic\n");
    content = cbs_allocate((size_t)size + 1);
    if (fread(content, 1, (size_t)size, stream) != (size_t)size) {
        free(content);
        return cbs_duplicate("runtime operation failed without a diagnostic\n");
    }
    content[size] = '\0';
    return content;
}

static int execute_captured(const CbsNode *operation,
                            const CbsExecutionContext *context,
                            char **diagnostic)
{
    FILE *capture = tmpfile();
    int saved;
    int result;

    *diagnostic = NULL;
    if (capture == NULL)
        return execute_operation(operation, context);
    saved = dup(STDERR_FILENO);
    if (saved < 0 || dup2(fileno(capture), STDERR_FILENO) < 0) {
        if (saved >= 0)
            close(saved);
        fclose(capture);
        return execute_operation(operation, context);
    }
    result = execute_operation(operation, context);
    fflush(stderr);
    dup2(saved, STDERR_FILENO);
    close(saved);
    if (!result)
        *diagnostic = capture_stream(capture);
    fclose(capture);
    return result;
}

static void subordinate_note(const CbsNode *operation,
                             const CbsExecutionContext *context,
                             const char *diagnostic, int allowed)
{
    char first_line[768];
    const char *newline = strchr(diagnostic, '\n');
    size_t length = newline == NULL ? strlen(diagnostic) :
                    (size_t)(newline - diagnostic);
    char message[1024];

    if (length >= sizeof(first_line))
        length = sizeof(first_line) - 1;
    memcpy(first_line, diagnostic, length);
    first_line[length] = '\0';
    snprintf(message, sizeof(message),
             "on_fail diagnostic failed%s; original failure remains primary; %s",
             allowed ? " with allow_failure" : "", first_line);
    cbs_diagnostic(context->recipe_path, context->recipe_source,
                   operation->location, "note", "CPDL-N4001",
                   CBS_DIAG_RUNTIME, message);
}

static void environment_add(EnvironmentList *list, const char *name,
                            char *value)
{
    size_t capacity;
    if (list->count == list->capacity) {
        capacity = list->capacity == 0 ? 8 : list->capacity * 2;
        list->items = cbs_reallocate(list->items,
                                     capacity * sizeof(*list->items));
        list->capacity = capacity;
    }
    list->items[list->count].name = name;
    list->items[list->count].value = value;
    ++list->count;
}

static void environment_destroy(EnvironmentList *list)
{
    size_t index;
    for (index = list->inherited_count; index < list->count; ++index)
        free((char *)list->items[index].value);
    free(list->items);
}

static int execute_diagnostics(const CbsNode *on_fail,
                               const CbsExecutionContext *context)
{
    size_t index;
    for (index = 0; index < on_fail->child_count; ++index) {
        const CbsNode *operation = on_fail->children[index];
        char *diagnostic = NULL;
        int allowed = operation->kind == CBS_NODE_RUN &&
                      run_allows_failure(operation);
        if (!execute_captured(operation, context, &diagnostic)) {
            subordinate_note(operation, context,
                             diagnostic == NULL ? "no diagnostic" : diagnostic,
                             allowed);
            free(diagnostic);
            if (!allowed)
                return 0;
        }
    }
    return 1;
}

static int execute_block_internal(const CbsNode *block,
                                  const CbsExecutionContext *context)
{
    CbsExecutionContext local = *context;
    EnvironmentList environment;
    const CbsNode *on_fail = NULL;
    size_t index;
    int result = 1;

    memset(&environment, 0, sizeof(environment));
    if (block->child_count > 0 &&
        block->children[block->child_count - 1]->kind == CBS_NODE_ON_FAIL)
        on_fail = block->children[block->child_count - 1];
    for (index = 0; index < context->environment_count; ++index)
        environment_add(&environment, context->environment[index].name,
                        (char *)context->environment[index].value);
    environment.inherited_count = environment.count;
    local.environment = environment.items;
    local.environment_count = environment.count;
    for (index = 0; index < block->child_count; ++index) {
        const CbsNode *operation = block->children[index];
        char *diagnostic = NULL;

        if (operation->kind == CBS_NODE_ON_FAIL) {
            break;
        }
        if (operation->kind == CBS_NODE_ENV) {
            char *value = cbs_resolve_value(operation->value, operation->flag,
                                            &local);
            environment_add(&environment, operation->name, value);
            local.environment = environment.items;
            local.environment_count = environment.count;
            continue;
        }
        if (!execute_captured(operation, &local, &diagnostic)) {
            if (diagnostic != NULL)
                fputs(diagnostic, stderr);
            free(diagnostic);
            result = 0;
            break;
        }
    }
    if (!result && on_fail != NULL)
        execute_diagnostics(on_fail, &local);
    environment_destroy(&environment);
    return result;
}

static int execute_cd(const CbsNode *operation,
                      const CbsExecutionContext *context)
{
    CbsExecutionContext nested = *context;
    struct stat status;
    char *path = cbs_resolve_confined_path(operation->value, context);
    int result;

    if (path == NULL || lstat(path, &status) != 0 ||
        !S_ISDIR(status.st_mode) || S_ISLNK(status.st_mode)) {
        char message[512];
        snprintf(message, sizeof(message), "cannot enter directory `%s`; errno=%d",
                 operation->value, errno);
        cbs_diagnostic(context->recipe_path, context->recipe_source,
                       operation->location, "error", "CPDL-E4004",
                       CBS_DIAG_RUNTIME, message);
        free(path);
        return 0;
    }
    nested.working_directory = path;
    result = execute_block_internal(operation, &nested);
    free(path);
    return result;
}

static int execute_operation(const CbsNode *operation,
                             const CbsExecutionContext *context)
{
    if (operation->kind == CBS_NODE_RUN)
        return cbs_execute_run(operation, context);
    if (is_filesystem(operation->kind))
        return cbs_execute_filesystem(operation, context);
    if (operation->kind == CBS_NODE_REPLACE ||
        operation->kind == CBS_NODE_INSERT ||
        operation->kind == CBS_NODE_REQUIRE)
        return cbs_execute_edit_assertion(operation, context);
    if (operation->kind == CBS_NODE_CD)
        return execute_cd(operation, context);
    cbs_diagnostic(context->recipe_path, context->recipe_source,
                   operation->location, "error", "CPDL-E9001",
                   CBS_DIAG_INTERNAL, "operation has no runtime executor");
    return 0;
}

int cbs_execute_block(const CbsNode *block,
                      const CbsExecutionContext *context)
{
    return execute_block_internal(block, context);
}
