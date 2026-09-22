/* Confined filesystem operations exposed by CPDL. */
#define _POSIX_C_SOURCE 200809L

#include "cbs.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

typedef struct {
    /* Matched path strings owned by this list. */
    char **items;
    /* Number of matched paths. */
    size_t count;
    /* Allocated pointer capacity. */
    size_t capacity;
} PathList;

/* Forward-declare parent confinement validation. */
static int safe_parents(const char *path, const char *root);
static unsigned char *read_regular(const char *path, size_t *length,
                                   mode_t *mode);
static int atomic_write_bytes(const char *path, const unsigned char *content,
                              size_t length, mode_t mode);

/* Emit one located filesystem-operation diagnostic. */
static void fs_error(const CbsNode *operation,
                     const CbsExecutionContext *context,
                     const char *logical_path, const char *detail) {
    char message[1024];

    snprintf(message, sizeof(message), "%s: `%s`; errno=%d (%s)", detail,
             logical_path == NULL ? "" : logical_path, errno, strerror(errno));
    cbs_diagnostic(context->recipe_path, context->recipe_source,
                   operation->location, "error", "CPDL-E4004", CBS_DIAG_RUNTIME,
                   message);
}

/* Infer whether a copy source names a file, directory, or glob. */
static int inferred_kind(const char *value) {
    return value != NULL && value[0] == '$' && value[1] != '{'
               ? CBS_TOKEN_CBS_VALUE
               : CBS_TOKEN_STRING;
}

/* Join two path components without losing the root separator. */
static char *join_path(const char *left, const char *right) {
    size_t left_length = strlen(left);
    size_t right_length = strlen(right);
    int separator = left_length > 0 && left[left_length - 1] != '/';
    char *result =
        cbs_allocate(left_length + (size_t)separator + right_length + 1);

    memcpy(result, left, left_length);
    if (separator)
        result[left_length++] = '/';
    memcpy(result + left_length, right, right_length + 1);
    return result;
}

/* Normalize separators and dot components in an absolute path. */
static char *normalize_absolute(const char *path) {
    char *copy;
    char *cursor;
    char **parts;
    size_t count = 0;
    size_t capacity = strlen(path) / 2 + 2;
    size_t length = 1;
    size_t index;
    char *result;

    if (path[0] != '/') {
        errno = EINVAL;
        return NULL;
    }
    copy = cbs_duplicate(path);
    parts = cbs_allocate(capacity * sizeof(*parts));
    cursor = copy;
    while (*cursor != '\0') {
        char *start;
        while (*cursor == '/')
            ++cursor;
        if (*cursor == '\0')
            break;
        start = cursor;
        while (*cursor != '\0' && *cursor != '/')
            ++cursor;
        if (*cursor != '\0')
            *cursor++ = '\0';
        if (strcmp(start, ".") == 0)
            continue;
        if (strcmp(start, "..") == 0) {
            if (count == 0) {
                free(parts);
                free(copy);
                errno = EPERM;
                return NULL;
            }
            --count;
            continue;
        }
        parts[count++] = start;
        length += strlen(start) + 1;
    }
    result = cbs_allocate(length + 1);
    result[0] = '/';
    result[1] = '\0';
    for (index = 0; index < count; ++index) {
        if (index != 0)
            strcat(result, "/");
        strcat(result, parts[index]);
    }
    free(parts);
    free(copy);
    return result;
}

/* Check whether a canonical path is beneath a canonical root. */
static int beneath(const char *path, const char *root) {
    size_t length = strlen(root);

    if (strcmp(root, "/") == 0)
        return 1;
    return strncmp(path, root, length) == 0 &&
           (path[length] == '\0' || path[length] == '/');
}

/* Select the execution root corresponding to a logical recipe path. */
static const char *containing_root(const char *path,
                                   const CbsExecutionContext *context) {
    const char *roots[3];
    const char *best = NULL;
    size_t index;

    roots[0] = context->src;
    roots[1] = context->build;
    roots[2] = context->dest;
    for (index = 0; index < 3; ++index) {
        if (roots[index] != NULL && beneath(path, roots[index]) &&
            (best == NULL || strlen(roots[index]) > strlen(best)))
            best = roots[index];
    }
    return best;
}

/* Reject unsafe or unexpectedly broad filesystem roots. */
static int safe_root(const char *root) {
    char *normalized;
    struct stat status;
    int result;

    if (root == NULL || root[0] != '/') {
        errno = EINVAL;
        return 0;
    }
    normalized = normalize_absolute(root);
    result = normalized != NULL && strcmp(normalized, root) == 0 &&
             lstat(root, &status) == 0 && S_ISDIR(status.st_mode) &&
             !S_ISLNK(status.st_mode);
    free(normalized);
    if (!result)
        errno = EPERM;
    return result;
}

/* Resolve a recipe path beneath its permitted execution root. */
static char *resolve_path(const char *logical,
                          const CbsExecutionContext *context,
                          const char **root_out) {
    char *expanded =
        cbs_resolve_value(logical, inferred_kind(logical), context);
    char *joined = expanded[0] == '/'
                       ? cbs_duplicate(expanded)
                       : join_path(context->working_directory, expanded);
    char *normalized = normalize_absolute(joined);
    const char *root;

    free(expanded);
    free(joined);
    if (normalized == NULL)
        return NULL;
    root = containing_root(normalized, context);
    if (root == NULL || !safe_root(root)) {
        free(normalized);
        errno = EPERM;
        return NULL;
    }
    if (root_out != NULL)
        *root_out = root;
    return normalized;
}

char *cbs_resolve_confined_path(const char *logical,
                                const CbsExecutionContext *context) {
    const char *root;
    char *path = resolve_path(logical, context, &root);

    if (path == NULL)
        return NULL;
    if (!safe_root(root) || !safe_parents(path, root)) {
        free(path);
        return NULL;
    }
    return path;
}

/* Verify that every existing parent remains within the root. */
static int safe_parents(const char *path, const char *root) {
    char *copy = cbs_duplicate(path);
    char *cursor = copy + strlen(root);
    struct stat status;

    while (*cursor != '\0') {
        char saved;
        while (*cursor == '/')
            ++cursor;
        if (*cursor == '\0')
            break;
        cursor = strchr(cursor, '/');
        if (cursor == NULL)
            break;
        saved = *cursor;
        *cursor = '\0';
        if (lstat(copy, &status) == 0) {
            if (!S_ISDIR(status.st_mode) || S_ISLNK(status.st_mode)) {
                free(copy);
                errno = ELOOP;
                return 0;
            }
        } else if (errno != ENOENT) {
            free(copy);
            return 0;
        }
        *cursor = saved;
    }
    free(copy);
    return 1;
}

/* Parse a recipe mode or return the operation's fallback mode. */
static mode_t parse_mode(const char *text, mode_t fallback) {
    char *end;
    unsigned long value;

    if (text == NULL)
        return fallback;
    value = strtoul(text, &end, 8);
    return *end == '\0' ? (mode_t)value : fallback;
}

/* Create missing parent directories under a confined root. */
static int ensure_directories(const char *path, const char *root, mode_t mode) {
    char *copy = cbs_duplicate(path);
    char *cursor = copy + strlen(root);
    struct stat status;

    while (1) {
        char *slash;
        char saved;
        while (*cursor == '/')
            ++cursor;
        slash = strchr(cursor, '/');
        if (slash == NULL)
            slash = copy + strlen(copy);
        saved = *slash;
        *slash = '\0';
        if (lstat(copy, &status) != 0) {
            if (errno != ENOENT || mkdir(copy, mode) != 0) {
                free(copy);
                return 0;
            }
        } else if (!S_ISDIR(status.st_mode) || S_ISLNK(status.st_mode)) {
            free(copy);
            errno = EEXIST;
            return 0;
        }
        *slash = saved;
        if (saved == '\0')
            break;
        cursor = slash + 1;
    }
    free(copy);
    return chmod(path, mode) == 0;
}

/* Return the final component of a path. */
static const char *base_name(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash == NULL ? path : slash + 1;
}

/* Resolve whether a copy destination names a file or directory target. */
static char *destination_path(const char *source, const char *destination) {
    struct stat status;

    if (lstat(destination, &status) == 0 && S_ISDIR(status.st_mode))
        return join_path(destination, base_name(source));
    return cbs_duplicate(destination);
}

/* Copy file descriptors until end of input. */
static int copy_bytes(int input, int output) {
    char buffer[32768];
    ssize_t received;

    while (1) {
        received = read(input, buffer, sizeof(buffer));
        if (received < 0 && errno == EINTR)
            continue;
        if (received <= 0)
            break;
        ssize_t offset = 0;
        while (offset < received) {
            ssize_t written =
                write(output, buffer + offset, (size_t)(received - offset));
            if (written < 0 && errno == EINTR)
                continue;
            if (written < 0)
                return 0;
            offset += written;
        }
    }
    return received == 0;
}

/* Copy one regular file while preserving the requested mode. */
static int copy_one(const char *source, const char *destination) {
    struct stat source_status;
    struct stat destination_status;
    char *actual = destination_path(source, destination);
    int result = 0;

    if (lstat(source, &source_status) != 0)
        goto done;
    if (S_ISDIR(source_status.st_mode)) {
        errno = EISDIR;
        goto done;
    }
    if (S_ISLNK(source_status.st_mode)) {
        size_t capacity = source_status.st_size > 0
                              ? (size_t)source_status.st_size + 1
                              : 4096;
        char *target = cbs_allocate(capacity + 1);
        ssize_t length = readlink(source, target, capacity);
        if (length < 0) {
            free(target);
            goto done;
        }
        target[length] = '\0';
        if (lstat(actual, &destination_status) == 0 && unlink(actual) != 0) {
            free(target);
            goto done;
        }
        result = symlink(target, actual) == 0;
        free(target);
    } else if (S_ISREG(source_status.st_mode)) {
        int input;
        int output;
        if (lstat(actual, &destination_status) == 0) {
            if (S_ISLNK(destination_status.st_mode)) {
                if (unlink(actual) != 0)
                    goto done;
            } else if (!S_ISREG(destination_status.st_mode)) {
                errno = EINVAL;
                goto done;
            }
        } else if (errno != ENOENT) {
            goto done;
        }
        input = open(source, O_RDONLY | O_NOFOLLOW);
        if (input < 0)
            goto done;
        output = open(actual, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW,
                      source_status.st_mode & 07777);
        if (output >= 0) {
            result = copy_bytes(input, output) &&
                     fchmod(output, source_status.st_mode & 07777) == 0;
            if (close(output) != 0)
                result = 0;
        }
        close(input);
    } else {
        errno = EINVAL;
    }
done:
    free(actual);
    return result;
}

/* Recursively copy a directory's contents without dereferencing symlinks. */
static int copy_tree(const char *source, const char *destination) {
    DIR *directory;
    struct dirent *entry;
    struct stat source_status;
    mode_t source_mode;

    if (lstat(source, &source_status) != 0 ||
        !S_ISDIR(source_status.st_mode) || S_ISLNK(source_status.st_mode)) {
        errno = ENOTDIR;
        return 0;
    }
    source_mode = source_status.st_mode & 07777;
    if (lstat(destination, &source_status) == 0) {
        if (!S_ISDIR(source_status.st_mode) || S_ISLNK(source_status.st_mode)) {
            errno = ENOTDIR;
            return 0;
        }
    } else if (errno != ENOENT || mkdir(destination, source_mode) != 0) {
        return 0;
    }
    directory = opendir(source);
    if (directory == NULL)
        return 0;
    while ((entry = readdir(directory)) != NULL) {
        char *source_child;
        char *destination_child;
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0)
            continue;
        source_child = join_path(source, entry->d_name);
        destination_child = join_path(destination, entry->d_name);
        if (lstat(source_child, &source_status) != 0 ||
            (S_ISDIR(source_status.st_mode) && !S_ISLNK(source_status.st_mode)
                 ? copy_tree(source_child, destination_child)
                 : copy_one(source_child, destination)) == 0) {
            free(source_child);
            free(destination_child);
            closedir(directory);
            return 0;
        }
        if (S_ISDIR(source_status.st_mode) && !S_ISLNK(source_status.st_mode) &&
            chmod(destination_child, source_status.st_mode & 07777) != 0) {
            free(source_child);
            free(destination_child);
            closedir(directory);
            return 0;
        }
        free(source_child);
        free(destination_child);
    }
    closedir(directory);
    return chmod(destination, source_mode) == 0;
}

/* Materialize one verified named source into the build tree. */
int cbs_execute_materialize(const CbsNode *operation,
                            const CbsExecutionContext *context) {
    char *source =
        cbs_resolve_value(operation->value, CBS_TOKEN_CBS_VALUE, context);
    char *destination;
    const char *root;
    size_t index;
    struct stat status;
    int declared = 0;
    int result;

    if (source == NULL)
        goto failure;
    for (index = 0; index < context->source_count; ++index)
        if (context->sources[index].path != NULL &&
            strcmp(source, context->sources[index].path) == 0) {
            declared = 1;
            break;
        }
    if (!declared || lstat(source, &status) != 0 || !S_ISREG(status.st_mode) ||
        S_ISLNK(status.st_mode))
        goto failure;
    destination = resolve_path(operation->second_value, context, &root);
    if (destination == NULL || !safe_parents(destination, root)) {
        free(destination);
        goto failure;
    }
    result = copy_one(source, destination);
    if (!result)
        fs_error(operation, context, operation->second_value,
                 "source materialization failed");
    free(source);
    free(destination);
    return result;

failure:
    fs_error(operation, context, operation->value,
             "verified source cannot be materialized");
    free(source);
    return 0;
}

/* Remove a confined tree recursively for the remove operation. */
static int remove_tree(const char *path) {
    struct stat status;
    DIR *directory;
    struct dirent *entry;

    if (lstat(path, &status) != 0)
        return 0;
    if (!S_ISDIR(status.st_mode) || S_ISLNK(status.st_mode))
        return unlink(path) == 0;
    directory = opendir(path);
    if (directory == NULL)
        return 0;
    while ((entry = readdir(directory)) != NULL) {
        char *child;
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        child = join_path(path, entry->d_name);
        if (!remove_tree(child)) {
            free(child);
            closedir(directory);
            return 0;
        }
        free(child);
    }
    if (closedir(directory) != 0)
        return 0;
    return rmdir(path) == 0;
}

/* Match one byte against a bracket-expression glob class. */
static int class_match(const char **pattern, unsigned char byte) {
    const char *cursor = *pattern + 1;
    int negate = *cursor == '!';
    int matched = 0;

    if (negate)
        ++cursor;
    while (*cursor != '\0' && *cursor != ']') {
        unsigned char first = (unsigned char)*cursor++;
        if (first == '\\' && *cursor != '\0')
            first = (unsigned char)*cursor++;
        if (*cursor == '-' && cursor[1] != '\0' && cursor[1] != ']') {
            unsigned char last;
            ++cursor;
            last = (unsigned char)*cursor++;
            if (last == '\\' && *cursor != '\0')
                last = (unsigned char)*cursor++;
            if (byte >= first && byte <= last)
                matched = 1;
        } else if (byte == first) {
            matched = 1;
        }
    }
    if (*cursor == ']')
        ++cursor;
    *pattern = cursor;
    return negate ? !matched : matched;
}

/* Match a path against the supported glob syntax. */
static int glob_match(const char *pattern, const char *text) {
    if (*pattern == '\0')
        return *text == '\0';
    if (pattern[0] == '*' && pattern[1] == '*' &&
        (pattern[2] == '/' || pattern[2] == '\0')) {
        const char *rest = pattern + 2;
        const char *cursor = text;
        if (*rest == '\0')
            return 1;
        if (*rest == '/')
            ++rest;
        if (glob_match(rest, text))
            return 1;
        while (*cursor != '\0') {
            if (*cursor++ == '/' && glob_match(rest, cursor))
                return 1;
        }
        return 0;
    }
    if (*pattern == '*') {
        const char *cursor = text;
        do {
            if (glob_match(pattern + 1, cursor))
                return 1;
        } while (*cursor != '\0' && *cursor++ != '/');
        return 0;
    }
    if (*text == '\0')
        return 0;
    if (*pattern == '?')
        return *text != '/' && glob_match(pattern + 1, text + 1);
    if (*pattern == '[') {
        const char *next = pattern;
        return *text != '/' && class_match(&next, (unsigned char)*text) &&
               glob_match(next, text + 1);
    }
    if (*pattern == '\\' && pattern[1] != '\0')
        ++pattern;
    return *pattern == *text && glob_match(pattern + 1, text + 1);
}

/* Append one matched path to a dynamically growing list. */
static void path_list_add(PathList *list, const char *path) {
    size_t capacity;
    if (list->count == list->capacity) {
        capacity = list->capacity == 0 ? 16 : list->capacity * 2;
        list->items =
            cbs_reallocate(list->items, capacity * sizeof(*list->items));
        list->capacity = capacity;
    }
    list->items[list->count++] = cbs_duplicate(path);
}

/* Recursively collect paths matching a glob pattern. */
static void collect_matches(const char *directory, const char *pattern,
                            PathList *matches) {
    DIR *stream = opendir(directory);
    struct dirent *entry;

    if (stream == NULL)
        return;
    while ((entry = readdir(stream)) != NULL) {
        char *path;
        struct stat status;
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        path = join_path(directory, entry->d_name);
        if (lstat(path, &status) == 0) {
            if (glob_match(pattern, path))
                path_list_add(matches, path);
            if (S_ISDIR(status.st_mode) && !S_ISLNK(status.st_mode))
                collect_matches(path, pattern, matches);
        }
        free(path);
    }
    closedir(stream);
}

/* Sort matched paths deterministically by their textual names. */
static int compare_paths(const void *left, const void *right) {
    const char *const *a = left;
    const char *const *b = right;
    return strcmp(*a, *b);
}

/* Release all paths collected during glob expansion. */
static void path_list_destroy(PathList *list) {
    size_t index;
    for (index = 0; index < list->count; ++index)
        free(list->items[index]);
    free(list->items);
}

/* Select source paths for copy/remove operations. */
static void assertion_error(const CbsNode *operation,
                            const CbsExecutionContext *context,
                            const char *message);

static int select_paths(const CbsNode *operation,
                        const CbsExecutionContext *context, PathList *paths) {
    const char *root;
    char *resolved = resolve_path(operation->value, context, &root);

    if (resolved == NULL)
        return 0;
    if (operation->flag) {
        collect_matches(root, resolved, paths);
        qsort(paths->items, paths->count, sizeof(*paths->items), compare_paths);
        free(resolved);
        if (paths->count == 0) {
            errno = ENOENT;
            return 0;
        }
    } else {
        path_list_add(paths, resolved);
        free(resolved);
    }
    return 1;
}

/* Resolve one confined glob and publish its match as `${glob.NAME}`. */
int cbs_execute_glob_binding(const CbsNode *operation,
                             const CbsExecutionContext *context) {
    const char *root;
    char *pattern = resolve_path(operation->value, context, &root);
    PathList paths;
    CbsExecutionContext *mutable_context = (CbsExecutionContext *)context;
    size_t next;

    memset(&paths, 0, sizeof(paths));
    if (pattern == NULL || !safe_parents(pattern, root))
        goto failure;
    collect_matches(root, pattern, &paths);
    qsort(paths.items, paths.count, sizeof(*paths.items), compare_paths);
    if (paths.count != (size_t)operation->number) {
        char message[256];
        snprintf(message, sizeof(message),
                 "glob binding expected %ld matches but found %lu",
                 operation->number, (unsigned long)paths.count);
        assertion_error(operation, context, message);
        free(pattern);
        path_list_destroy(&paths);
        return 0;
    }
    next = mutable_context->glob_binding_count;
    if (next == mutable_context->glob_binding_capacity) {
        size_t capacity = next == 0 ? 4 : next * 2;
        mutable_context->glob_bindings = cbs_reallocate(
            mutable_context->glob_bindings,
            capacity * sizeof(*mutable_context->glob_bindings));
        mutable_context->glob_binding_capacity = capacity;
    }
    mutable_context->glob_bindings[next].name = operation->name;
    mutable_context->glob_bindings[next].value = cbs_duplicate(paths.items[0]);
    mutable_context->glob_binding_count++;
    free(pattern);
    path_list_destroy(&paths);
    return 1;

failure:
    errno = ENOENT;
    fs_error(operation, context, operation->value, "glob binding failed");
    free(pattern);
    path_list_destroy(&paths);
    return 0;
}

typedef struct {
    const CbsNode *operation;
    const CbsExecutionContext *context;
    int failed;
    char seen[128][256];
    size_t seen_count;
} LinkCheck;

static int check_link_dependency(const char *dependency, void *user) {
    LinkCheck *check = user;
    size_t index;
    if (check->seen_count < sizeof(check->seen) / sizeof(check->seen[0]))
        snprintf(check->seen[check->seen_count++],
                 sizeof(check->seen[0]), "%s", dependency);
    for (index = 0; index < check->operation->child_count; ++index) {
        const CbsNode *property = check->operation->children[index];
        if (property == NULL || property->name == NULL ||
            property->value == NULL)
            continue;
        if (strcmp(property->name, "needs") == 0 &&
            strcmp(property->value, dependency) == 0)
            return 1;
        if (strcmp(property->name, "forbids") == 0 &&
            strcmp(property->value, dependency) == 0) {
            char message[256];
            snprintf(message, sizeof(message),
                     "artifact links forbidden library `%s`", dependency);
            assertion_error(check->operation, check->context, message);
            check->failed = 1;
            return 0;
        }
    }
    return 1;
}

static void link_check_destroy(LinkCheck *check) { (void)check; }

int cbs_execute_links(const CbsNode *operation,
                      const CbsExecutionContext *context) {
    const char *root;
    char *path = resolve_path(operation->value, context, &root);
    LinkCheck check = {operation, context, 0, {0}, 0};
    size_t index;

    if (path == NULL || !safe_parents(path, root)) {
        fs_error(operation, context, operation->value,
                 "cannot inspect ELF dynamic dependencies");
        free(path);
        return 0;
    }
    if (!cbs_observe_dependencies(check_link_dependency, path, &check)) {
        if (check.failed) {
            link_check_destroy(&check);
            free(path);
            return 0;
        }
        fs_error(operation, context, operation->value,
                 "cannot inspect ELF dynamic dependencies");
        link_check_destroy(&check);
        free(path);
        return 0;
    }
    if (check.failed) {
        link_check_destroy(&check);
        free(path);
        return 0;
    }
    for (index = 0; index < operation->child_count; ++index) {
        const CbsNode *property = operation->children[index];
        if (property == NULL || property->name == NULL)
            continue;
        if (strcmp(property->name, "needs") == 0) {
            size_t seen;
            int found = 0;
            for (seen = 0; seen < check.seen_count; ++seen)
                if (strcmp(check.seen[seen], property->value) == 0)
                    found = 1;
            if (!found) {
                char message[256];
                snprintf(message, sizeof(message),
                         "artifact is missing required library `%s`",
                         property->value);
                assertion_error(operation, context, message);
                link_check_destroy(&check);
                free(path);
                return 0;
            }
        }
    }
    link_check_destroy(&check);
    free(path);
    return 1;
}

typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} PatchLines;

static void patch_lines_destroy(PatchLines *lines) {
    size_t index;
    for (index = 0; index < lines->count; ++index)
        free(lines->items[index]);
    free(lines->items);
}

static int patch_lines_add(PatchLines *lines, const char *value, size_t length) {
    if (lines->count == lines->capacity) {
        size_t capacity = lines->capacity == 0 ? 16 : lines->capacity * 2;
        lines->items = cbs_reallocate(lines->items,
                                      capacity * sizeof(*lines->items));
        lines->capacity = capacity;
    }
    lines->items[lines->count++] = cbs_duplicate_range(value, length);
    return 1;
}

static int patch_split_lines(const unsigned char *data, size_t length,
                             PatchLines *lines, int *trailing_newline) {
    size_t start = 0;
    *trailing_newline = length > 0 && data[length - 1] == '\n';
    while (start < length) {
        size_t end = start;
        while (end < length && data[end] != '\n')
            ++end;
        if (end > start && data[end - 1] == '\r')
            --end;
        patch_lines_add(lines, (const char *)data + start, end - start);
        start = end < length ? end + 1 : end;
    }
    return 1;
}

static int patch_path(const char *header, long strip, char *output,
                      size_t output_size) {
    const char *path = header;
    long index;
    while (*path == ' ')
        ++path;
    for (index = 0; index < strip; ++index) {
        path = strchr(path, '/');
        if (path == NULL)
            return 0;
        ++path;
    }
    return *path != '\0' && snprintf(output, output_size, "%s", path) <
           (int)output_size;
}

static int patch_apply_lines(PatchLines *source, PatchLines *patch,
                             size_t first_hunk, PatchLines *result,
                             int trailing_newline) {
    size_t source_index = 0;
    size_t patch_index = first_hunk;
    while (patch_index < patch->count) {
        size_t old_start, old_count, new_start, new_count;
        size_t hunk_source;
        int parsed = sscanf(patch->items[patch_index], "@@ -%zu,%zu +%zu,%zu",
                            &old_start, &old_count, &new_start, &new_count);
        if (parsed != 4) {
            old_count = new_count = 1;
            if (sscanf(patch->items[patch_index], "@@ -%zu +%zu", &old_start,
                       &new_start) != 2)
                return 0;
        }
        (void)new_start;
        if (old_start == 0 || old_start - 1 < source_index ||
            old_start - 1 > source->count)
            return 0;
        while (source_index < old_start - 1) {
            patch_lines_add(result, source->items[source_index],
                            strlen(source->items[source_index]));
            ++source_index;
        }
        ++patch_index;
        hunk_source = source_index;
        while (patch_index < patch->count && patch->items[patch_index][0] !=
                                                   '@') {
            const char *line = patch->items[patch_index++];
            if (line[0] == '\\')
                continue;
            if (line[0] != ' ' && line[0] != '-' && line[0] != '+')
                return 0;
            if (line[0] == '+' ) {
                patch_lines_add(result, line + 1, strlen(line + 1));
            } else {
                if (source_index >= source->count ||
                    strcmp(source->items[source_index], line + 1) != 0)
                    return 0;
                if (line[0] == ' ')
                    patch_lines_add(result, source->items[source_index],
                                     strlen(source->items[source_index]));
                ++source_index;
            }
        }
        if (source_index - hunk_source != old_count)
            return 0;
    }
    while (source_index < source->count)
        patch_lines_add(result, source->items[source_index],
                        strlen(source->items[source_index++]));
    (void)trailing_newline;
    return 1;
}

int cbs_execute_patch(const CbsNode *operation,
                      const CbsExecutionContext *context) {
    char file_path[4096], target_relative[4096], target_path[4096];
    char recipe_directory[4096], digest[65];
    const char *slash = strrchr(context->recipe_path, '/');
    FILE *file;
    long length;
    unsigned char *data;
    PatchLines patch = {0}, source = {0}, result = {0};
    int patch_newline, source_newline;
    size_t index, header = 0;
    mode_t mode;
    unsigned char *source_data;
    size_t source_length, result_length;
    int success = 0;

    snprintf(recipe_directory, sizeof(recipe_directory), "%.*s",
             slash == NULL ? 1 : (int)(slash - context->recipe_path),
             slash == NULL ? "." : context->recipe_path);
    snprintf(file_path, sizeof(file_path), "%s/%s", recipe_directory,
             operation->name);
    if (!cbs_digest_file(file_path, digest) || strcmp(digest, operation->value) != 0)
        goto failure;
    file = fopen(file_path, "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0 ||
        (length = ftell(file)) < 0 || fseek(file, 0, SEEK_SET) != 0)
        goto failure;
    data = cbs_allocate((size_t)length + 1);
    if (fread(data, 1, (size_t)length, file) != (size_t)length) {
        fclose(file); free(data); goto failure;
    }
    fclose(file);
    patch_split_lines(data, (size_t)length, &patch, &patch_newline);
    free(data);
    if (patch.count < 3 || strncmp(patch.items[0], "--- ", 4) != 0 ||
        strncmp(patch.items[1], "+++ ", 4) != 0 ||
        !patch_path(patch.items[1] + 4, operation->number, target_relative,
                    sizeof(target_relative)))
        goto failure;
    snprintf(target_path, sizeof(target_path), "%s/%s", context->src,
             target_relative);
    source_data = read_regular(target_path, &source_length, &mode);
    if (source_data == NULL)
        goto failure;
    patch_split_lines(source_data, source_length, &source, &source_newline);
    free(source_data);
    for (index = 2; index < patch.count; ++index)
        if (strncmp(patch.items[index], "@@ ", 3) == 0) {
            header = index;
            break;
        }
    if (header == 0 || !patch_apply_lines(&source, &patch, header, &result,
                                          source_newline))
        goto failure;
    result_length = 0;
    for (index = 0; index < result.count; ++index)
        result_length += strlen(result.items[index]) + 1;
    if (!source_newline && result_length > 0)
        --result_length;
    data = cbs_allocate(result_length + 1);
    result_length = 0;
    for (index = 0; index < result.count; ++index) {
        size_t item_length = strlen(result.items[index]);
        memcpy(data + result_length, result.items[index], item_length);
        result_length += item_length;
        if (index + 1 < result.count || source_newline)
            data[result_length++] = '\n';
    }
    data[result_length] = '\0';
    if (!atomic_write_bytes(target_path, data, result_length, mode)) {
        free(data); goto failure;
    }
    free(data);
    success = 1;
failure:
    if (!success)
        fs_error(operation, context, operation->name, "patch application failed");
    patch_lines_destroy(&patch);
    patch_lines_destroy(&source);
    patch_lines_destroy(&result);
    return success;
}

/* Write replacement content through a temporary file and rename. */
static int atomic_write_bytes(const char *path, const unsigned char *content,
                              size_t length, mode_t mode) {
    static const char suffix[] = "/.cbs-write-XXXXXX";
    char *template;
    const char *slash = strrchr(path, '/');
    size_t directory_length = slash == NULL ? 0 : (size_t)(slash - path);
    int descriptor;
    int result;

    template = cbs_allocate(directory_length + sizeof(suffix));
    memcpy(template, path, directory_length);
    memcpy(template + directory_length, suffix, sizeof(suffix));
    descriptor = mkstemp(template);
    if (descriptor < 0) {
        free(template);
        return 0;
    }
    {
        size_t offset = 0;
        result = 1;
        while (offset < length) {
            ssize_t written =
                write(descriptor, content + offset, length - offset);
            if (written < 0 && errno == EINTR)
                continue;
            if (written < 0) {
                result = 0;
                break;
            }
            offset += (size_t)written;
        }
    }
    if (result && fchmod(descriptor, mode) != 0)
        result = 0;
    if (close(descriptor) != 0)
        result = 0;
    if (result && rename(template, path) != 0)
        result = 0;
    if (!result)
        unlink(template);
    free(template);
    return result;
}

/* Execute one confined filesystem operation. */
/* Ship one shared library from the build sandbox's library directories. The
 * candidates are the build image's layout, not a recipe path, so this is the
 * one operation that reads outside the confined roots (ADR-0036). */
static int stage_library(const CbsNode *operation,
                         const CbsExecutionContext *context) {
    static const char *const plain[] = {"/usr/lib", "/lib", "/usr/lib64",
                                        "/lib64"};
    char *name = cbs_resolve_value(operation->value, CBS_TOKEN_STRING, context);
    char *triplet = cbs_resolve_value("${triplet}", CBS_TOKEN_STRING, context);
    const char *root;
    char *directory = NULL;
    char searched[1024];
    char candidate[4096];
    struct stat status;
    size_t index;
    size_t searched_length = 0;
    int found = 0;
    int result = 0;

    if (name[0] == '\0' || strchr(name, '/') != NULL ||
        strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        errno = EINVAL;
        fs_error(operation, context, name,
                 "stage library name must be a bare file name");
        goto done;
    }
    searched[0] = '\0';
    for (index = 0; index < 2 + sizeof(plain) / sizeof(plain[0]); ++index) {
        int multiarch = index < 2;
        if (multiarch && triplet[0] == '\0')
            continue;
        if (multiarch)
            snprintf(candidate, sizeof(candidate), "%s/%s/%s",
                     index == 0 ? "/usr/lib" : "/lib", triplet, name);
        else
            snprintf(candidate, sizeof(candidate), "%s/%s", plain[index - 2],
                     name);
        searched_length += (size_t)snprintf(
            searched + searched_length, sizeof(searched) - searched_length,
            "%s%.*s", searched_length == 0 ? "" : ", ",
            (int)(strlen(candidate) - strlen(name) - 1), candidate);
        if (searched_length >= sizeof(searched))
            searched_length = sizeof(searched) - 1;
        if (lstat(candidate, &status) == 0 &&
            (S_ISREG(status.st_mode) || S_ISLNK(status.st_mode))) {
            found = 1;
            break;
        }
    }
    if (!found) {
        char detail[1400];
        snprintf(detail, sizeof(detail),
                 "library is not in the build sandbox (searched %s); declare "
                 "the build dependency that provides it",
                 searched);
        errno = ENOENT;
        fs_error(operation, context, name, detail);
        goto done;
    }
    directory = resolve_path(operation->second_value, context, &root);
    if (directory == NULL || !safe_parents(directory, root)) {
        fs_error(operation, context, operation->second_value,
                 "stage destination is not a confined directory");
        goto done;
    }
    if (lstat(directory, &status) != 0) {
        if (errno != ENOENT || !ensure_directories(directory, root, 0755)) {
            fs_error(operation, context, operation->second_value,
                     "cannot create stage destination");
            goto done;
        }
    } else if (!S_ISDIR(status.st_mode) || S_ISLNK(status.st_mode)) {
        errno = ENOTDIR;
        fs_error(operation, context, operation->second_value,
                 "stage destination is not a directory");
        goto done;
    }
    if (!copy_one(candidate, directory)) {
        fs_error(operation, context, candidate, "cannot stage library");
        goto done;
    }
    result = 1;
done:
    free(directory);
    free(triplet);
    free(name);
    return result;
}

int cbs_execute_filesystem(const CbsNode *operation,
                           const CbsExecutionContext *context) {
    char *first = NULL;
    char *second = NULL;
    const char *root = NULL;
    PathList paths;
    size_t index;
    int result = 0;

    memset(&paths, 0, sizeof(paths));
    if (operation->kind == CBS_NODE_MKDIR ||
        operation->kind == CBS_NODE_WRITE) {
        first = resolve_path(operation->value, context, &root);
        if (first == NULL || !safe_parents(first, root))
            goto failure;
    }
    if (operation->kind == CBS_NODE_MKDIR) {
        result = ensure_directories(first, root,
                                    parse_mode(operation->second_value, 0755));
    } else if (operation->kind == CBS_NODE_WRITE) {
        const char *mode_text =
            operation->child_count == 0 ? NULL : operation->children[0]->value;
        second = cbs_resolve_value(operation->second_value, operation->flag,
                                   context);
        result =
            atomic_write_bytes(first, (const unsigned char *)second,
                               strlen(second), parse_mode(mode_text, 0644));
    } else if (operation->kind == CBS_NODE_STAGE) {
        return stage_library(operation, context);
    } else if (operation->kind == CBS_NODE_SYMLINK) {
        first = resolve_path(operation->second_value, context, &root);
        second = cbs_resolve_value(operation->value,
                                   inferred_kind(operation->value), context);
        if (first == NULL || !safe_parents(first, root))
            goto failure;
        result = symlink(second, first) == 0;
    } else {
        if (!select_paths(operation, context, &paths))
            goto failure;
        if (operation->kind == CBS_NODE_COPY ||
            operation->kind == CBS_NODE_MOVE) {
            second = resolve_path(operation->second_value, context, &root);
            if (second == NULL || !safe_parents(second, root))
                goto failure;
            if (operation->kind == CBS_NODE_COPY && operation->number) {
                struct stat source_status;
                if (paths.count != 1 || lstat(paths.items[0], &source_status) != 0 ||
                    !S_ISDIR(source_status.st_mode) ||
                    S_ISLNK(source_status.st_mode))
                    goto failure;
                result = copy_tree(paths.items[0], second);
                if (!result)
                    goto failure;
                free(first);
                free(second);
                path_list_destroy(&paths);
                return 1;
            }
            if (paths.count > 1) {
                struct stat status;
                if (lstat(second, &status) != 0 || !S_ISDIR(status.st_mode)) {
                    errno = ENOTDIR;
                    goto failure;
                }
            }
        }
        result = 1;
        for (index = 0; index < paths.count && result; ++index) {
            const char *path_root =
                containing_root(paths.items[index], context);
            if (path_root == NULL ||
                !safe_parents(paths.items[index], path_root)) {
                result = 0;
            } else if (operation->kind == CBS_NODE_COPY) {
                result = copy_one(paths.items[index], second);
            } else if (operation->kind == CBS_NODE_MOVE) {
                char *actual = destination_path(paths.items[index], second);
                result = rename(paths.items[index], actual) == 0;
                free(actual);
            } else if (operation->kind == CBS_NODE_REMOVE) {
                struct stat status;
                if (lstat(paths.items[index], &status) != 0)
                    result = 0;
                else if (S_ISDIR(status.st_mode) && !S_ISLNK(status.st_mode))
                    result = operation->number == 1
                                 ? remove_tree(paths.items[index])
                                 : rmdir(paths.items[index]) == 0;
                else
                    result = unlink(paths.items[index]) == 0;
            } else if (operation->kind == CBS_NODE_CHMOD) {
                struct stat status;
                if (lstat(paths.items[index], &status) != 0) {
                    result = 0;
                } else if (S_ISLNK(status.st_mode)) {
                    errno = EOPNOTSUPP;
                    result = 0;
                } else {
                    result = chmod(paths.items[index],
                                   parse_mode(operation->second_value, 0)) == 0;
                }
            }
        }
    }
    if (!result)
        goto failure;
    free(first);
    free(second);
    path_list_destroy(&paths);
    return 1;

failure:
    fs_error(operation, context, operation->value,
             "filesystem operation failed");
    free(first);
    free(second);
    path_list_destroy(&paths);
    return operation->second_flag ? 1 : 0;
}

/* Report a source-edit or assertion failure at its operation location. */
static void assertion_error(const CbsNode *operation,
                            const CbsExecutionContext *context,
                            const char *message) {
    cbs_diagnostic(context->recipe_path, context->recipe_source,
                   operation->location, "error", "CPDL-E4005", CBS_DIAG_RUNTIME,
                   message);
}

/* Read a regular file for source-edit matching. */
static unsigned char *read_regular(const char *path, size_t *length,
                                   mode_t *mode) {
    struct stat status;
    unsigned char *content;
    size_t offset = 0;
    int descriptor;

    if (lstat(path, &status) != 0 || !S_ISREG(status.st_mode)) {
        if (errno == 0)
            errno = EINVAL;
        return NULL;
    }
    if (status.st_size < 0 || (unsigned long long)status.st_size >
                                  (unsigned long long)((size_t)-1) - 1) {
        errno = EFBIG;
        return NULL;
    }
    descriptor = open(path, O_RDONLY | O_NOFOLLOW);
    if (descriptor < 0)
        return NULL;
    *length = (size_t)status.st_size;
    content = cbs_allocate(*length + 1);
    while (offset < *length) {
        ssize_t received = read(descriptor, content + offset, *length - offset);
        if (received < 0 && errno == EINTR)
            continue;
        if (received <= 0) {
            close(descriptor);
            free(content);
            errno = received == 0 ? EIO : errno;
            return NULL;
        }
        offset += (size_t)received;
    }
    if (close(descriptor) != 0) {
        free(content);
        return NULL;
    }
    content[*length] = '\0';
    *mode = status.st_mode & 07777;
    return content;
}

/* Count non-overlapping occurrences of a byte pattern. */
/* How far a source-edit match extends past its literal prefix. */
enum { EDIT_UNTIL_NONE, EDIT_UNTIL_WHITESPACE, EDIT_UNTIL_LINE };

/* Read the operation's `until` clause, if any. */
static int edit_until_mode(const CbsNode *operation) {
    size_t index;
    for (index = 0; index < operation->child_count; ++index) {
        const CbsNode *property = operation->children[index];
        if (property->kind == CBS_NODE_PROPERTY && property->name != NULL &&
            strcmp(property->name, "until") == 0 && property->value != NULL)
            return strcmp(property->value, "line") == 0
                       ? EDIT_UNTIL_LINE
                       : EDIT_UNTIL_WHITESPACE;
    }
    return EDIT_UNTIL_NONE;
}

/* Length of the match at OFFSET whose literal prefix already matched: the
 * prefix alone, or the prefix plus every byte before the first delimiter
 * (or the end of the content). */
static size_t match_extent(const unsigned char *content, size_t content_length,
                           size_t offset, size_t needle_length, int until) {
    size_t end = offset + needle_length;
    if (until == EDIT_UNTIL_NONE)
        return needle_length;
    while (end < content_length) {
        unsigned char byte = content[end];
        if (byte == '\n' || byte == '\r')
            break;
        if (until == EDIT_UNTIL_WHITESPACE && (byte == ' ' || byte == '\t'))
            break;
        ++end;
    }
    return end - offset;
}

/* Count non-overlapping matches of a byte pattern, each extended by the
 * until rule. */
static size_t count_matches(const unsigned char *content,
                            size_t content_length, const unsigned char *needle,
                            size_t needle_length, int until) {
    size_t count = 0;
    size_t offset = 0;

    if (needle_length == 0)
        return 0;
    while (offset + needle_length <= content_length) {
        if (memcmp(content + offset, needle, needle_length) == 0) {
            ++count;
            offset += match_extent(content, content_length, offset,
                                   needle_length, until);
        } else {
            ++offset;
        }
    }
    return count;
}

static size_t count_bytes(const unsigned char *content, size_t content_length,
                          const unsigned char *needle, size_t needle_length) {
    return count_matches(content, content_length, needle, needle_length,
                         EDIT_UNTIL_NONE);
}

/* Test whether a byte pattern occurs at least once. */
static int contains_bytes(const unsigned char *content, size_t content_length,
                          const unsigned char *needle, size_t needle_length) {
    if (needle_length == 0)
        return 1;
    return count_bytes(content, content_length, needle, needle_length) > 0;
}

/* Build edited file content for replace or insert operations. The result
 * never exceeds the content plus one replacement per match. */
static unsigned char *
edited_content(const unsigned char *content, size_t content_length,
               const unsigned char *needle, size_t needle_length,
               const unsigned char *replacement, size_t replacement_length,
               size_t matches, int insert, int insert_before, int until,
               size_t *result_length) {
    size_t bound;
    unsigned char *result;
    size_t source_offset = 0;
    size_t result_offset = 0;

    if (replacement_length != 0 &&
        matches > (((size_t)-1) - content_length - 1) / replacement_length) {
        errno = EOVERFLOW;
        return NULL;
    }
    bound = content_length + matches * replacement_length + 1;
    result = cbs_allocate(bound);
    while (source_offset < content_length) {
        if (source_offset + needle_length <= content_length &&
            memcmp(content + source_offset, needle, needle_length) == 0) {
            size_t extent = match_extent(content, content_length,
                                         source_offset, needle_length, until);
            if (insert && insert_before) {
                memcpy(result + result_offset, replacement, replacement_length);
                result_offset += replacement_length;
            }
            if (insert) {
                memcpy(result + result_offset, needle, needle_length);
                result_offset += needle_length;
            }
            if (!insert || !insert_before) {
                memcpy(result + result_offset, replacement, replacement_length);
                result_offset += replacement_length;
            }
            source_offset += extent;
        } else {
            result[result_offset++] = content[source_offset++];
        }
    }
    result[result_offset] = '\0';
    *result_length = result_offset;
    return result;
}

/* Keep the bytes before the one exact truncation marker. */
static unsigned char *truncated_content(const unsigned char *content,
                                        size_t content_length,
                                        const unsigned char *needle,
                                        size_t needle_length,
                                        size_t *result_length) {
    size_t offset;
    unsigned char *result;

    for (offset = 0; offset + needle_length <= content_length; ++offset)
        if (memcmp(content + offset, needle, needle_length) == 0) {
            result = cbs_allocate(offset + 1);
            memcpy(result, content, offset);
            result[offset] = '\0';
            *result_length = offset;
            return result;
        }
    errno = ENOENT;
    return NULL;
}

/* Apply one cardinality-checked source edit atomically. */
static int execute_edit(const CbsNode *operation,
                        const CbsExecutionContext *context) {
    const char *root;
    char *path = resolve_path(operation->name, context, &root);
    char *needle =
        cbs_resolve_value(operation->value, operation->flag, context);
    char *replacement = cbs_resolve_value(operation->second_value,
                                          operation->second_flag, context);
    unsigned char *content = NULL;
    unsigned char *result = NULL;
    size_t content_length = 0;
    size_t result_length = 0;
    size_t matches;
    mode_t mode = 0;
    char message[256];
    int until = edit_until_mode(operation);
    int success = 0;

    if (operation->selector_glob) {
        PathList paths;
        const char *pattern_root;
        char *pattern = resolve_path(operation->name, context, &pattern_root);
        size_t total_matches = 0;
        size_t index;
        memset(&paths, 0, sizeof(paths));
        if (pattern == NULL) {
            fs_error(operation, context, operation->name,
                     "source edit glob resolution failed");
            return 0;
        }
        collect_matches(pattern_root, pattern, &paths);
        qsort(paths.items, paths.count, sizeof(*paths.items), compare_paths);
        free(pattern);
        if (paths.count == 0) {
            errno = ENOENT;
            fs_error(operation, context, operation->name,
                     "source edit glob matched no paths");
            path_list_destroy(&paths);
            return 0;
        }
        needle = cbs_resolve_value(operation->value, operation->flag, context);
        for (index = 0; index < paths.count; ++index) {
            unsigned char *matched_content;
            size_t matched_length;
            mode_t matched_mode;
            matched_content = read_regular(paths.items[index], &matched_length,
                                           &matched_mode);
            if (matched_content == NULL) {
                fs_error(operation, context, paths.items[index],
                         "source edit glob target is not a regular file");
                path_list_destroy(&paths);
                free(needle);
                return 0;
            }
            total_matches += count_matches(
                matched_content, matched_length,
                (const unsigned char *)needle, strlen(needle), until);
            free(matched_content);
        }
        if (total_matches != (size_t)operation->number) {
            char message[256];
            snprintf(message, sizeof(message),
                     "source edit expected %ld matches but found %lu",
                     operation->number, (unsigned long)total_matches);
            assertion_error(operation, context, message);
            path_list_destroy(&paths);
            free(needle);
            return 0;
        }
        free(needle);
        for (index = 0; index < paths.count; ++index) {
            CbsNode single = *operation;
            unsigned char *current_content;
            size_t current_length;
            mode_t current_mode;
            char *current_needle;
            single.selector_glob = 0;
            single.name = paths.items[index];
            current_needle =
                cbs_resolve_value(operation->value, operation->flag, context);
            current_content = read_regular(single.name, &current_length,
                                           &current_mode);
            if (current_content == NULL) {
                free(current_needle);
                path_list_destroy(&paths);
                return 0;
            }
            single.number = (long)count_matches(
                current_content, current_length,
                (const unsigned char *)current_needle,
                strlen(current_needle), until);
            free(current_content);
            free(current_needle);
            if (!execute_edit(&single, context)) {
                path_list_destroy(&paths);
                return 0;
            }
        }
        path_list_destroy(&paths);
        return 1;
    }

    if (path == NULL || !safe_parents(path, root))
        goto filesystem_failure;
    content = read_regular(path, &content_length, &mode);
    if (content == NULL)
        goto filesystem_failure;
    matches = count_matches(content, content_length,
                            (const unsigned char *)needle, strlen(needle),
                            until);
    if (matches != (size_t)operation->number) {
        snprintf(message, sizeof(message),
                 "source edit expected %ld matches but found %lu",
                 operation->number, (unsigned long)matches);
        assertion_error(operation, context, message);
        goto done;
    }
    if (operation->kind == CBS_NODE_TRUNCATE)
        result = truncated_content(content, content_length,
                                   (const unsigned char *)needle,
                                   strlen(needle), &result_length);
    else
        result = edited_content(
            content, content_length, (const unsigned char *)needle,
            strlen(needle), (const unsigned char *)replacement,
            strlen(replacement), matches, operation->kind == CBS_NODE_INSERT,
            operation->insert_before, until, &result_length);
    if (result == NULL)
        goto filesystem_failure;
    if (!atomic_write_bytes(path, result, result_length, mode))
        goto filesystem_failure;
    success = 1;
    goto done;

filesystem_failure:
    fs_error(operation, context, operation->name, "source edit failed");
done:
    free(path);
    free(needle);
    free(replacement);
    free(content);
    free(result);
    return success;
}

/* Evaluate a glob assertion and its expected cardinality. */
static int require_glob(const CbsNode *operation,
                        const CbsExecutionContext *context) {
    const char *root;
    char *pattern = resolve_path(operation->value, context, &root);
    PathList matches;
    char message[256];
    int success;

    memset(&matches, 0, sizeof(matches));
    if (pattern == NULL) {
        fs_error(operation, context, operation->value,
                 "assertion path resolution failed");
        return 0;
    }
    collect_matches(root, pattern, &matches);
    qsort(matches.items, matches.count, sizeof(*matches.items), compare_paths);
    success = matches.count == (size_t)operation->number;
    if (!success) {
        snprintf(message, sizeof(message),
                 "glob expected %ld matches but found %lu", operation->number,
                 (unsigned long)matches.count);
        assertion_error(operation, context, message);
    }
    free(pattern);
    path_list_destroy(&matches);
    return success;
}

/* Emit one located assertion failure naming the required path. */
static void require_failure(const CbsNode *operation,
                            const CbsExecutionContext *context,
                            const char *detail) {
    char message[4096 + 512];
    snprintf(message, sizeof(message), "required %s `%s` %s", operation->name,
             operation->value, detail);
    assertion_error(operation, context, message);
}

/* Say what an existing path is when it is not what the assertion wants. */
static const char *entry_description(const struct stat *status) {
    if (S_ISLNK(status->st_mode))
        return "is a symbolic link";
    if (S_ISDIR(status->st_mode))
        return "is a directory";
    if (S_ISREG(status->st_mode))
        return "is a regular file";
    return "is neither a regular file, a directory, nor a symbolic link";
}

/* Evaluate existence, type, and content properties for one path. The
 * failure message says what was found; "does not exist" is reserved for a
 * path that is absent. */
static int require_path(const CbsNode *operation,
                        const CbsExecutionContext *context) {
    const char *root;
    char *path = resolve_path(operation->value, context, &root);
    struct stat status;
    unsigned char *content = NULL;
    size_t content_length = 0;
    mode_t mode;
    size_t index;
    char message[256];
    char detail[4096 + 256];
    int success = 0;

    if (path == NULL) {
        require_failure(operation, context,
                        "resolves outside the confined build roots");
        goto done;
    }
    if (!safe_parents(path, root)) {
        require_failure(operation, context,
                        "has a parent that is a symbolic link or not a "
                        "directory");
        goto done;
    }
    if (lstat(path, &status) != 0) {
        int error = errno;
        if (error == ENOENT)
            require_failure(operation, context, "does not exist");
        else if (error == ENOTDIR)
            require_failure(operation, context,
                            "has a parent component that is not a directory");
        else {
            snprintf(detail, sizeof(detail), "cannot be examined: %s",
                     strerror(error));
            require_failure(operation, context, detail);
        }
        goto done;
    }
    if (strcmp(operation->name, "directory") == 0) {
        if (!S_ISDIR(status.st_mode)) {
            snprintf(detail, sizeof(detail),
                     "%s; require directory matches directories only",
                     entry_description(&status));
            require_failure(operation, context, detail);
            goto done;
        }
        success = 1;
        goto done;
    }
    if (strcmp(operation->name, "symlink") == 0) {
        char link_target[4096];
        ssize_t length;
        if (!S_ISLNK(status.st_mode)) {
            snprintf(detail, sizeof(detail),
                     "%s; require symlink matches symbolic links only",
                     entry_description(&status));
            require_failure(operation, context, detail);
            goto done;
        }
        length = readlink(path, link_target, sizeof(link_target) - 1);
        if (length < 0) {
            snprintf(detail, sizeof(detail), "cannot be read: %s",
                     strerror(errno));
            require_failure(operation, context, detail);
            goto done;
        }
        link_target[length] = '\0';
        for (index = 0; index < operation->child_count; ++index) {
            const CbsNode *property = operation->children[index];
            char *expected = cbs_resolve_value(
                property->value, inferred_kind(property->value), context);
            int matches = strcmp(link_target, expected) == 0;
            if (!matches)
                snprintf(detail, sizeof(detail),
                         "points to `%s`, expected `%s`", link_target,
                         expected);
            free(expected);
            if (!matches) {
                require_failure(operation, context, detail);
                goto done;
            }
        }
        success = 1;
        goto done;
    }
    if (!S_ISREG(status.st_mode)) {
        snprintf(detail, sizeof(detail),
                 "%s; require file matches regular files only",
                 entry_description(&status));
        require_failure(operation, context, detail);
        goto done;
    }
    content = read_regular(path, &content_length, &mode);
    if (content == NULL) {
        snprintf(detail, sizeof(detail), "cannot be read: %s",
                 strerror(errno));
        require_failure(operation, context, detail);
        goto done;
    }
    for (index = 0; index < operation->child_count; ++index) {
        const CbsNode *property = operation->children[index];
        if (strcmp(property->name, "executable") == 0) {
            if ((mode & 0111) == 0) {
                assertion_error(operation, context,
                                "required file must be executable");
                goto done;
            }
            continue;
        }
        if (strcmp(property->name, "nonempty") == 0) {
            if (content_length == 0) {
                assertion_error(operation, context,
                                "required file must not be empty");
                goto done;
            }
            continue;
        }
        if (strcmp(property->name, "same_as") == 0) {
            char *other_path =
                cbs_resolve_confined_path(property->value, context);
            unsigned char *other = NULL;
            size_t other_length = 0;
            mode_t other_mode;
            int equal = other_path != NULL && safe_parents(other_path, root) &&
                        (other = read_regular(other_path, &other_length,
                                              &other_mode)) != NULL &&
                        other_length == content_length &&
                        memcmp(other, content, content_length) == 0;
            free(other);
            free(other_path);
            if (!equal) {
                assertion_error(operation, context,
                                "required file does not match same_as file");
                goto done;
            }
            continue;
        }
        char *needle =
            cbs_resolve_value(property->value, property->flag, context);
        int present =
            contains_bytes(content, content_length,
                           (const unsigned char *)needle, strlen(needle));
        free(needle);
        if (!present) {
            snprintf(message, sizeof(message),
                     "required file content %lu was not present",
                     (unsigned long)(index + 1));
            assertion_error(operation, context, message);
            goto done;
        }
    }
    success = 1;
done:
    free(content);
    free(path);
    return success;
}

/* Find the enabled/disabled state of one Kconfig symbol. */
static int config_state(const unsigned char *content, size_t length,
                        const char *symbol, const char *wanted) {
    size_t offset = 0;
    size_t symbol_length = strlen(symbol);
    int active = 0;
    int disabled = 0;

    while (offset < length) {
        size_t end = offset;
        while (end < length && content[end] != '\n')
            ++end;
        if (end >= symbol_length + 2 &&
            memcmp(content + offset, symbol, symbol_length) == 0 &&
            content[offset + symbol_length] == '=') {
            active = end - offset == symbol_length + 2 &&
                     content[offset + symbol_length + 1] == wanted[0];
            if (strcmp(wanted, "absent") == 0)
                return 0;
        }
        {
            static const char marker[] = "# ";
            size_t disabled_length = symbol_length + 13;
            if (end - offset == disabled_length &&
                memcmp(content + offset, marker, sizeof(marker) - 1) == 0 &&
                memcmp(content + offset + 2, symbol, symbol_length) == 0 &&
                memcmp(content + offset + 2 + symbol_length, " is not set",
                       11) == 0)
                disabled = 1;
        }
        offset = end < length ? end + 1 : end;
    }
    if (strcmp(wanted, "absent") == 0)
        return !active && !disabled;
    if (strcmp(wanted, "n") == 0)
        return active || disabled;
    return active;
}

/* Evaluate a Kconfig assertion against a generated configuration. */
static int require_config(const CbsNode *operation,
                          const CbsExecutionContext *context) {
    const char *root;
    char *path = resolve_path(operation->value, context, &root);
    unsigned char *content = NULL;
    size_t length = 0;
    mode_t mode;
    size_t index;
    char message[256];

    if (path == NULL || !safe_parents(path, root) ||
        (content = read_regular(path, &length, &mode)) == NULL) {
        assertion_error(operation, context,
                        "required config file does not exist");
        free(path);
        return 0;
    }
    for (index = 0; index < operation->child_count; ++index) {
        const CbsNode *property = operation->children[index];
        if (!config_state(content, length, property->name, property->value)) {
            snprintf(message, sizeof(message),
                     "config assertion failed: %s = %s", property->name,
                     property->value);
            assertion_error(operation, context, message);
            free(content);
            free(path);
            return 0;
        }
    }
    free(content);
    free(path);
    return 1;
}

/* Execute a source edit or require assertion operation. */
int cbs_execute_edit_assertion(const CbsNode *operation,
                               const CbsExecutionContext *context) {
    if (operation->kind == CBS_NODE_REPLACE ||
        operation->kind == CBS_NODE_INSERT ||
        operation->kind == CBS_NODE_TRUNCATE)
        return execute_edit(operation, context);
    if (operation->kind == CBS_NODE_REQUIRE) {
        if (strcmp(operation->name, "glob") == 0)
            return require_glob(operation, context);
        if (strcmp(operation->name, "config") == 0)
            return require_config(operation, context);
        return require_path(operation, context);
    }
    errno = EINVAL;
    fs_error(operation, context, operation->value,
             "invalid edit or assertion operation");
    return 0;
}
