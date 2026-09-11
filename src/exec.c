/* Child-process execution, limits, timeouts, and process-group cleanup. */
#define _POSIX_C_SOURCE 200809L

#include "cbs.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} StringList;

static volatile sig_atomic_t active_child;
static volatile sig_atomic_t interrupted;

/* Forward SIGINT to the active child process group. */
static void forward_interrupt(int signal_number) {
    (void)signal_number;
    interrupted = 1;
    if (active_child > 0)
        kill(-active_child, SIGTERM);
}

static int apply_limit(int resource, long value, long default_value,
                       unsigned long multiplier) {
    struct rlimit limit;
    unsigned long long selected = value > 0 ? (unsigned long long)value
                                            : (unsigned long long)default_value;
    if (selected > (unsigned long long)RLIM_INFINITY / multiplier)
        return 0;
    limit.rlim_cur = limit.rlim_max = (rlim_t)(selected * multiplier);
    return setrlimit(resource, &limit) == 0;
}

static int apply_child_limits(const CbsExecutionContext *context,
                              long timeout_ms) {
    long cpu = context->limits.cpu_seconds;
    if (cpu <= 0)
        cpu = timeout_ms > 0 ? (timeout_ms + 999) / 1000 + 1 : 86400;
    return apply_limit(RLIMIT_AS, context->limits.address_space_mb, 8192,
                       1024UL * 1024UL) &&
           apply_limit(RLIMIT_FSIZE, context->limits.file_size_mb, 16384,
                       1024UL * 1024UL) &&
           apply_limit(RLIMIT_CPU, cpu, cpu, 1) &&
           apply_limit(RLIMIT_NOFILE, context->limits.open_files, 4096, 1) &&
           apply_limit(RLIMIT_NPROC, context->limits.processes, 4096, 1);
}

/* Check whether a recipe attempts to invoke a prohibited shell utility. */
int cbs_is_forbidden_executable(const char *value) {
    static const char *const forbidden[] = {"sh",  "bash", "dash", "ash",
                                            "ksh", "zsh",  "env"};
    const char *base = strrchr(value, '/');
    size_t index;

    base = base == NULL ? value : base + 1;
    for (index = 0; index < sizeof(forbidden) / sizeof(forbidden[0]); ++index) {
        if (strcmp(base, forbidden[index]) == 0)
            return 1;
    }
    return 0;
}

/* Check whether a compiler name violates the toolchain policy. */
int cbs_is_forbidden_compiler(const char *value) {
    const char *base = strrchr(value == NULL ? "" : value, '/');
    base = base == NULL ? value : base + 1;
    return base != NULL &&
           (strcmp(base, "cc") == 0 || strcmp(base, "gcc") == 0 ||
            strcmp(base, "clang") == 0 || strcmp(base, "g++") == 0 ||
            strcmp(base, "c++") == 0);
}

/* Append an owned string to a dynamically growing argument list. */
static void string_list_add(StringList *list, char *value) {
    size_t capacity;

    if (list->count == list->capacity) {
        capacity = list->capacity == 0 ? 8 : list->capacity * 2;
        list->items =
            cbs_reallocate(list->items, capacity * sizeof(*list->items));
        list->capacity = capacity;
    }
    list->items[list->count++] = value;
}

/* Add the NULL terminator required by execve. */
static void string_list_terminate(StringList *list) {
    string_list_add(list, NULL);
}

/* Release every string and pointer in an argument list. */
static void string_list_destroy(StringList *list) {
    size_t index;

    for (index = 0; index < list->count; ++index)
        free(list->items[index]);
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static const char *context_value(const CbsExecutionContext *context,
                                 const char *name, size_t length, char *number,
                                 size_t number_size) {
    size_t index;

    if (length == 4 && strncmp(name, "name", length) == 0)
        return context->name;
    if (length == 7 && strncmp(name, "version", length) == 0)
        return context->version;
    if (length == 7 && strncmp(name, "release", length) == 0) {
        snprintf(number, number_size, "%ld", context->release);
        return number;
    }
    if (length == 4 && strncmp(name, "arch", length) == 0)
        return context->arch;
    if (length == 7 && strncmp(name, "triplet", length) == 0) {
        if (strcmp(context->arch, "x86_64") == 0)
            return "x86_64-linux-gnu";
        if (strcmp(context->arch, "aarch64") == 0)
            return "aarch64-linux-gnu";
        if (strcmp(context->arch, "riscv64") == 0)
            return "riscv64-linux-gnu";
        return NULL;
    }
    if (length == 3 && strncmp(name, "src", length) == 0)
        return context->src;
    if (length == 5 && strncmp(name, "build", length) == 0)
        return context->build;
    if (length == 4 && strncmp(name, "dest", length) == 0)
        return context->dest;
    if (length == 4 && strncmp(name, "jobs", length) == 0) {
        snprintf(number, number_size, "%ld", context->jobs);
        return number;
    }
    if (length > 7 && strncmp(name, "source.", 7) == 0) {
        for (index = 0; index < context->source_count; ++index) {
            if (strlen(context->sources[index].name) == length - 7 &&
                strncmp(context->sources[index].name, name + 7, length - 7) ==
                    0)
                return context->sources[index].path;
        }
    }
    return NULL;
}

static void append_text(char **buffer, size_t *length, size_t *capacity,
                        const char *text, size_t text_length) {
    size_t required = *length + text_length + 1;
    size_t next_capacity;

    if (required > *capacity) {
        next_capacity = *capacity == 0 ? 32 : *capacity;
        while (next_capacity < required)
            next_capacity *= 2;
        *buffer = cbs_reallocate(*buffer, next_capacity);
        *capacity = next_capacity;
    }
    memcpy(*buffer + *length, text, text_length);
    *length += text_length;
    (*buffer)[*length] = '\0';
}

char *cbs_resolve_value(const char *value, int token_kind,
                        const CbsExecutionContext *context) {
    const char *cursor;
    char *result = NULL;
    size_t length = 0;
    size_t capacity = 0;
    char number[64];

    if (value == NULL)
        return cbs_duplicate("");
    if (token_kind == CBS_TOKEN_BLOCK_STRING)
        return cbs_duplicate(value);
    if (token_kind == CBS_TOKEN_CBS_VALUE) {
        const char *resolved = context_value(
            context, value + 1, strlen(value + 1), number, sizeof(number));
        return cbs_duplicate(resolved == NULL ? "" : resolved);
    }
    cursor = value;
    while (*cursor != '\0') {
        const char *opening = strstr(cursor, "${");
        const char *closing;
        const char *resolved;

        if (opening == NULL) {
            append_text(&result, &length, &capacity, cursor, strlen(cursor));
            break;
        }
        append_text(&result, &length, &capacity, cursor,
                    (size_t)(opening - cursor));
        closing = strchr(opening + 2, '}');
        if (closing == NULL)
            break;
        resolved = context_value(context, opening + 2,
                                 (size_t)(closing - (opening + 2)), number,
                                 sizeof(number));
        if (resolved != NULL)
            append_text(&result, &length, &capacity, resolved,
                        strlen(resolved));
        cursor = closing + 1;
    }
    if (result == NULL)
        result = cbs_duplicate("");
    return result;
}

/* Check whether an environment entry has a requested variable name. */
static int environment_has_name(const char *entry, const char *name) {
    size_t length = strlen(name);
    return strncmp(entry, name, length) == 0 && entry[length] == '=';
}

static void environment_set(StringList *environment, const char *name,
                            char *entry) {
    size_t index;

    for (index = 0; index < environment->count; ++index) {
        if (environment_has_name(environment->items[index], name)) {
            free(environment->items[index]);
            environment->items[index] = entry;
            return;
        }
    }
    string_list_add(environment, entry);
}

/* Build one NAME=value environment entry. */
static char *environment_entry(const char *name, const char *value) {
    size_t name_length = strlen(name);
    size_t value_length = strlen(value);
    char *entry = cbs_allocate(name_length + value_length + 2);

    memcpy(entry, name, name_length);
    entry[name_length] = '=';
    memcpy(entry + name_length + 1, value, value_length + 1);
    return entry;
}

static const char *environment_get(const StringList *environment,
                                   const char *name) {
    size_t index;
    size_t length = strlen(name);

    for (index = 0; index < environment->count; ++index) {
        if (environment_has_name(environment->items[index], name))
            return environment->items[index] + length + 1;
    }
    return NULL;
}

/* Join two path components with exactly one separator. */
static char *join_path(const char *left, const char *right) {
    size_t left_length = strlen(left);
    size_t right_length = strlen(right);
    int separator = left_length > 0 && left[left_length - 1] != '/';
    char *path =
        cbs_allocate(left_length + (size_t)separator + right_length + 1);

    memcpy(path, left, left_length);
    if (separator)
        path[left_length++] = '/';
    memcpy(path + left_length, right, right_length + 1);
    return path;
}

static char *resolve_executable(const char *program,
                                const char *working_directory,
                                const StringList *environment) {
    const char *path_value;
    const char *cursor;

    if (strchr(program, '/') != NULL) {
        if (program[0] == '/')
            return cbs_duplicate(program);
        return join_path(working_directory, program);
    }
    path_value = environment_get(environment, "PATH");
    if (path_value == NULL)
        return NULL;
    cursor = path_value;
    while (1) {
        const char *end = strchr(cursor, ':');
        size_t length = end == NULL ? strlen(cursor) : (size_t)(end - cursor);
        char *directory = length == 0 ? cbs_duplicate(working_directory)
                                      : cbs_duplicate_range(cursor, length);
        char *candidate = join_path(directory, program);
        free(directory);
        if (access(candidate, X_OK) == 0)
            return candidate;
        free(candidate);
        if (end == NULL)
            break;
        cursor = end + 1;
    }
    return NULL;
}

/* Parse a validated CPDL duration into milliseconds. */
static long duration_milliseconds(const char *duration) {
    char *end;
    long value = strtol(duration, &end, 10);

    if (strcmp(end, "ms") == 0)
        return value;
    if (strcmp(end, "s") == 0)
        return value * 1000;
    if (strcmp(end, "m") == 0)
        return value * 60 * 1000;
    return value * 60 * 60 * 1000;
}

static long elapsed_milliseconds(const struct timespec *start,
                                 const struct timespec *now) {
    long seconds = (long)(now->tv_sec - start->tv_sec);
    long nanoseconds = now->tv_nsec - start->tv_nsec;
    return seconds * 1000 + nanoseconds / 1000000;
}

/* Wait for a child, escalating from interrupt to timeout termination. */
static int wait_for_child(pid_t child, long timeout_ms, int *status) {
    struct timespec start;
    struct timespec now;
    struct timespec pause_time;
    pid_t result;

    if (timeout_ms <= 0) {
        while (waitpid(child, status, 0) < 0) {
            if (errno != EINTR)
                return -1;
            if (interrupted)
                break;
        }
        if (interrupted) {
            kill(-child, SIGKILL);
            while (waitpid(child, status, 0) < 0 && errno == EINTR)
                ;
            return 0;
        }
        return 1;
    }
    clock_gettime(CLOCK_MONOTONIC, &start);
    pause_time.tv_sec = 0;
    pause_time.tv_nsec = 10000000;
    while (1) {
        result = waitpid(child, status, WNOHANG);
        if (result == child)
            return 1;
        if (interrupted)
            break;
        if (result < 0)
            return -1;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (elapsed_milliseconds(&start, &now) >= timeout_ms)
            break;
        nanosleep(&pause_time, NULL);
    }
    kill(-child, SIGTERM);
    clock_gettime(CLOCK_MONOTONIC, &start);
    pause_time.tv_sec = 0;
    pause_time.tv_nsec = 10000000;
    while ((result = waitpid(child, status, WNOHANG)) == 0) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (elapsed_milliseconds(&start, &now) >= 1000)
            break;
        nanosleep(&pause_time, NULL);
    }
    if (result == 0) {
        kill(-child, SIGKILL);
        while (waitpid(child, status, 0) < 0 && errno == EINTR)
            ;
    }
    return 0;
}

static void runtime_error(const CbsNode *run,
                          const CbsExecutionContext *context, const char *code,
                          const char *message) {
    cbs_diagnostic(context->recipe_path, context->recipe_source, run->location,
                   "error", code, CBS_DIAG_RUNTIME, message);
}

/* Resolve and execute one CPDL run operation under resource limits. */
int cbs_execute_run(const CbsNode *run, const CbsExecutionContext *context) {
    StringList arguments;
    StringList environment;
    char *program;
    char *executable;
    long expected_status = 0;
    long timeout_ms = 0;
    size_t index;
    pid_t child;
    int status = 0;
    int wait_result;
    char message[512];

    memset(&arguments, 0, sizeof(arguments));
    memset(&environment, 0, sizeof(environment));
    program = cbs_resolve_value(run->value, run->flag, context);
    if (cbs_is_forbidden_executable(program)) {
        runtime_error(run, context, "CPDL-E4001",
                      "command interpreters are not valid run executables");
        free(program);
        return 0;
    }
    string_list_add(&arguments, cbs_duplicate(program));
    string_list_add(&environment, cbs_duplicate("PATH=/usr/bin:/bin"));
    for (index = 0; index < context->environment_count; ++index)
        environment_set(&environment, context->environment[index].name,
                        environment_entry(context->environment[index].name,
                                          context->environment[index].value));
    for (index = 0; index < run->child_count; ++index) {
        const CbsNode *item = run->children[index];
        if (item->kind == CBS_NODE_ARGUMENT) {
            string_list_add(&arguments, cbs_resolve_value(item->value,
                                                          item->flag, context));
        } else if (item->kind == CBS_NODE_RUN_ENV) {
            char *value = cbs_resolve_value(item->value, item->flag, context);
            environment_set(&environment, item->name,
                            environment_entry(item->name, value));
            free(value);
        } else if (item->kind == CBS_NODE_RUN_EXPECT) {
            expected_status = item->number;
        } else if (item->kind == CBS_NODE_RUN_TIMEOUT) {
            timeout_ms = duration_milliseconds(item->value);
        }
    }
    executable =
        resolve_executable(program, context->working_directory, &environment);
    if (executable == NULL) {
        snprintf(message, sizeof(message), "cannot resolve executable `%s`",
                 program);
        runtime_error(run, context, "CPDL-E4001", message);
        free(program);
        string_list_destroy(&arguments);
        string_list_destroy(&environment);
        return 0;
    }
    string_list_terminate(&arguments);
    string_list_terminate(&environment);
    child = fork();
    if (child < 0) {
        snprintf(message, sizeof(message), "cannot create process; errno=%d",
                 errno);
        runtime_error(run, context, "CPDL-E4001", message);
        free(executable);
        free(program);
        string_list_destroy(&arguments);
        string_list_destroy(&environment);
        return 0;
    }
    interrupted = 0;
    active_child = child;
    {
        struct sigaction action, old_action;
        memset(&action, 0, sizeof(action));
        action.sa_handler = forward_interrupt;
        sigemptyset(&action.sa_mask);
        sigaction(SIGINT, &action, &old_action);
        if (child == 0) {
            setpgid(0, 0);
            if (!apply_child_limits(context, timeout_ms))
                _exit(125);
            if (chdir(context->working_directory) != 0)
                _exit(126);
            execve(executable, arguments.items, environment.items);
            _exit(127);
        }
        setpgid(child, child);
        wait_result = wait_for_child(child, timeout_ms, &status);
        sigaction(SIGINT, &old_action, NULL);
    }
    active_child = 0;
    free(executable);
    free(program);
    string_list_destroy(&arguments);
    string_list_destroy(&environment);
    if (wait_result == 0) {
        runtime_error(run, context, "CPDL-E4003", "process timed out");
        return 0;
    }
    if (wait_result < 0) {
        snprintf(message, sizeof(message), "cannot wait for process; errno=%d",
                 errno);
        runtime_error(run, context, "CPDL-E4001", message);
        return 0;
    }
    if (WIFSIGNALED(status)) {
        snprintf(message, sizeof(message), "process terminated by signal %d",
                 WTERMSIG(status));
        runtime_error(run, context, "CPDL-E4002", message);
        return 0;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != expected_status) {
        snprintf(message, sizeof(message),
                 "process exited with status %d; expected %ld",
                 WIFEXITED(status) ? WEXITSTATUS(status) : -1, expected_status);
        runtime_error(run, context, "CPDL-E4001", message);
        return 0;
    }
    return 1;
}
