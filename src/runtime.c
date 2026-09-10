#define _POSIX_C_SOURCE 200809L

#include "cbs.h"

#include <dirent.h>
#include <string.h>

long cbs_effective_jobs(long requested, long cpu_budget, long administrator_limit)
{
    long result = requested > 0 ? requested : 1;
    if (cpu_budget > 0 && result > cpu_budget) result = cpu_budget;
    if (administrator_limit > 0 && result > administrator_limit) result = administrator_limit;
    return result > 0 ? result : 1;
}

int cbs_validate_stage_path(const char *path, const CbsStagePolicy *policy)
{
    const char *part;
    if (path == NULL || policy == NULL || (policy->reject_empty && path[0] == '\0')) return 0;
    if (policy->reject_absolute && path[0] == '/') return 0;
    if (!policy->reject_parent) return 1;
    part = path;
    while (*part) { const char *end = strchr(part, '/'); size_t n = end ? (size_t)(end-part) : strlen(part); if (n == 2 && strncmp(part, "..", 2) == 0) return 0; part = end ? end+1 : part+n; }
    return 1;
}

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
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

static const char *extract_name(const CbsNode *operation)
{
    size_t index;

    for (index = 0; index < operation->child_count; ++index)
        if (operation->children[index]->kind == CBS_NODE_PROPERTY &&
            operation->children[index]->name != NULL &&
            strcmp(operation->children[index]->name, "as") == 0)
            return operation->children[index]->value;
    return NULL;
}

static int extract_error(const CbsNode *operation,
                         const CbsExecutionContext *context,
                         const char *message)
{
    cbs_diagnostic(context->recipe_path, context->recipe_source,
                   operation->location, "error", "CPDL-E4006",
                   CBS_DIAG_SOURCE, message);
    return 0;
}

static int execute_extract(const CbsNode *operation,
                           const CbsExecutionContext *context)
{
    char *archive = cbs_resolve_value(operation->value,
                                      CBS_TOKEN_CBS_VALUE, context);
    char *destination = cbs_resolve_confined_path(operation->second_value,
                                                   context);
    const char *name = extract_name(operation);
    char temporary[4096];
    char final_path[4096];
    char selected[4096];
    DIR *directory;
    struct dirent *entry;
    int found = 0;
    int result;

    if (archive == NULL || destination == NULL) {
        free(archive);
        free(destination);
        return extract_error(operation, context,
                             "extract path is outside the build workspace");
    }
    if (name == NULL) {
        result = cbs_extract_archive(archive, destination, operation->value + 8,
                                     context->recipe_path, context->recipe_source,
                                     operation->location);
        free(archive);
        free(destination);
        return result;
    }
    if (name[0] == '\0' || strchr(name, '/') != NULL ||
        strcmp(name, ".") == 0 || strcmp(name, "..") == 0 ||
        snprintf(temporary, sizeof(temporary), "%s/.cbs-extract-%ld",
                 destination, (long)getpid()) >= (int)sizeof(temporary)) {
        free(archive);
        free(destination);
        return extract_error(operation, context,
                             "extract `as` name is not a safe directory name");
    }
    if (mkdir(temporary, 0700) != 0) {
        free(archive);
        free(destination);
        return extract_error(operation, context,
                             "cannot create temporary extraction directory");
    }
    result = cbs_extract_archive(archive, temporary, operation->value + 8,
                                 context->recipe_path, context->recipe_source,
                                 operation->location);
    free(archive);
    if (!result) {
        rmdir(temporary);
        free(destination);
        return 0;
    }

    directory = opendir(temporary);
    if (directory == NULL) {
        free(destination);
        return extract_error(operation, context,
                             "cannot inspect extracted top-level directory");
    }
    while ((entry = readdir(directory)) != NULL) {
        struct stat status;

        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0)
            continue;
        if (found++ != 0 ||
            snprintf(selected, sizeof(selected), "%s/%s", temporary,
                     entry->d_name) >= (int)sizeof(selected) ||
            lstat(selected, &status) != 0 || !S_ISDIR(status.st_mode) ||
            S_ISLNK(status.st_mode)) {
            closedir(directory);
            free(destination);
            return extract_error(operation, context,
                                 "`as` requires one top-level directory");
        }
    }
    closedir(directory);
    if (found != 1 ||
        snprintf(final_path, sizeof(final_path), "%s/%s", destination, name) >=
            (int)sizeof(final_path) || lstat(final_path, &(struct stat){0}) == 0) {
        free(destination);
        return extract_error(operation, context,
                             "extracted destination already exists or is missing");
    }
    /* selected is the only entry beneath the temporary directory. */
    if (rename(selected, final_path) != 0 || rmdir(temporary) != 0) {
        free(destination);
        return extract_error(operation, context,
                             "cannot rename extracted top-level directory");
    }
    free(destination);
    return 1;
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
    if (operation->kind == CBS_NODE_EXTRACT)
        return execute_extract(operation, context);
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
