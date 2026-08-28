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
    char **items;
    size_t count;
    size_t capacity;
} PathList;

static void fs_error(const CbsNode *operation,
                     const CbsExecutionContext *context,
                     const char *logical_path, const char *detail)
{
    char message[1024];

    snprintf(message, sizeof(message), "%s: `%s`; errno=%d (%s)", detail,
             logical_path == NULL ? "" : logical_path, errno, strerror(errno));
    cbs_diagnostic(context->recipe_path, context->recipe_source,
                   operation->location, "error", "CPDL-E4004",
                   CBS_DIAG_RUNTIME, message);
}

static int inferred_kind(const char *value)
{
    return value != NULL && value[0] == '$' && value[1] != '{' ?
           CBS_TOKEN_CBS_VALUE : CBS_TOKEN_STRING;
}

static char *join_path(const char *left, const char *right)
{
    size_t left_length = strlen(left);
    size_t right_length = strlen(right);
    int separator = left_length > 0 && left[left_length - 1] != '/';
    char *result = cbs_allocate(left_length + (size_t)separator +
                                right_length + 1);

    memcpy(result, left, left_length);
    if (separator)
        result[left_length++] = '/';
    memcpy(result + left_length, right, right_length + 1);
    return result;
}

static char *normalize_absolute(const char *path)
{
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

static int beneath(const char *path, const char *root)
{
    size_t length = strlen(root);

    if (strcmp(root, "/") == 0)
        return 1;
    return strncmp(path, root, length) == 0 &&
           (path[length] == '\0' || path[length] == '/');
}

static const char *containing_root(const char *path,
                                   const CbsExecutionContext *context)
{
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

static int safe_root(const char *root)
{
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

static char *resolve_path(const char *logical, const CbsExecutionContext *context,
                          const char **root_out)
{
    char *expanded = cbs_resolve_value(logical, inferred_kind(logical), context);
    char *joined = expanded[0] == '/' ? cbs_duplicate(expanded) :
                   join_path(context->working_directory, expanded);
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

static int safe_parents(const char *path, const char *root)
{
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

static mode_t parse_mode(const char *text, mode_t fallback)
{
    char *end;
    unsigned long value;

    if (text == NULL)
        return fallback;
    value = strtoul(text, &end, 8);
    return *end == '\0' ? (mode_t)value : fallback;
}

static int ensure_directories(const char *path, const char *root, mode_t mode)
{
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

static const char *base_name(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash == NULL ? path : slash + 1;
}

static char *destination_path(const char *source, const char *destination)
{
    struct stat status;

    if (lstat(destination, &status) == 0 && S_ISDIR(status.st_mode))
        return join_path(destination, base_name(source));
    return cbs_duplicate(destination);
}

static int copy_bytes(int input, int output)
{
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
            ssize_t written = write(output, buffer + offset,
                                    (size_t)(received - offset));
            if (written < 0 && errno == EINTR)
                continue;
            if (written < 0)
                return 0;
            offset += written;
        }
    }
    return received == 0;
}

static int copy_one(const char *source, const char *destination)
{
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
        size_t capacity = source_status.st_size > 0 ?
                          (size_t)source_status.st_size + 1 : 4096;
        char *target = cbs_allocate(capacity + 1);
        ssize_t length = readlink(source, target, capacity);
        if (length < 0) {
            free(target);
            goto done;
        }
        target[length] = '\0';
        if (lstat(actual, &destination_status) == 0 &&
            unlink(actual) != 0) {
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

static int remove_tree(const char *path)
{
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
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0)
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

static int class_match(const char **pattern, unsigned char byte)
{
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

static int glob_match(const char *pattern, const char *text)
{
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

static void path_list_add(PathList *list, const char *path)
{
    size_t capacity;
    if (list->count == list->capacity) {
        capacity = list->capacity == 0 ? 16 : list->capacity * 2;
        list->items = cbs_reallocate(list->items,
                                     capacity * sizeof(*list->items));
        list->capacity = capacity;
    }
    list->items[list->count++] = cbs_duplicate(path);
}

static void collect_matches(const char *directory, const char *pattern,
                            PathList *matches)
{
    DIR *stream = opendir(directory);
    struct dirent *entry;

    if (stream == NULL)
        return;
    while ((entry = readdir(stream)) != NULL) {
        char *path;
        struct stat status;
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0)
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

static int compare_paths(const void *left, const void *right)
{
    const char *const *a = left;
    const char *const *b = right;
    return strcmp(*a, *b);
}

static void path_list_destroy(PathList *list)
{
    size_t index;
    for (index = 0; index < list->count; ++index)
        free(list->items[index]);
    free(list->items);
}

static int select_paths(const CbsNode *operation,
                        const CbsExecutionContext *context, PathList *paths)
{
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

static int atomic_write(const char *path, const char *content, mode_t mode)
{
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
        size_t length = strlen(content);
        size_t offset = 0;
        result = 1;
        while (offset < length) {
            ssize_t written = write(descriptor, content + offset, length - offset);
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

int cbs_execute_filesystem(const CbsNode *operation,
                           const CbsExecutionContext *context)
{
    char *first = NULL;
    char *second = NULL;
    const char *root = NULL;
    PathList paths;
    size_t index;
    int result = 0;

    memset(&paths, 0, sizeof(paths));
    if (operation->kind == CBS_NODE_MKDIR || operation->kind == CBS_NODE_WRITE) {
        first = resolve_path(operation->value, context, &root);
        if (first == NULL || !safe_parents(first, root))
            goto failure;
    }
    if (operation->kind == CBS_NODE_MKDIR) {
        result = ensure_directories(first, root,
                                    parse_mode(operation->second_value, 0755));
    } else if (operation->kind == CBS_NODE_WRITE) {
        const char *mode_text = operation->child_count == 0 ? NULL :
                                operation->children[0]->value;
        second = cbs_resolve_value(operation->second_value, operation->flag,
                                   context);
        result = atomic_write(first, second, parse_mode(mode_text, 0644));
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
        if (operation->kind == CBS_NODE_COPY || operation->kind == CBS_NODE_MOVE) {
            second = resolve_path(operation->second_value, context, &root);
            if (second == NULL || !safe_parents(second, root))
                goto failure;
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
            const char *path_root = containing_root(paths.items[index], context);
            if (path_root == NULL || !safe_parents(paths.items[index], path_root)) {
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
                    result = operation->number == 1 ? remove_tree(paths.items[index]) :
                             rmdir(paths.items[index]) == 0;
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
    fs_error(operation, context, operation->value, "filesystem operation failed");
    free(first);
    free(second);
    path_list_destroy(&paths);
    return 0;
}
