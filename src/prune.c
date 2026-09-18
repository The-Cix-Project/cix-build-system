/* Data-driven staged-tree normalization and removal reporting. */
#define _POSIX_C_SOURCE 200809L

#include "cbs.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} PathList;

static int add_path(PathList *list, const char *path) {
    if (list->count == list->capacity) {
        size_t next = list->capacity == 0 ? 16 : list->capacity * 2;
        list->items = cbs_reallocate(list->items, next * sizeof(*list->items));
        list->capacity = next;
    }
    list->items[list->count++] = cbs_duplicate(path);
    return 1;
}

static void free_paths(PathList *list) {
    size_t index;
    for (index = 0; index < list->count; ++index)
        free(list->items[index]);
    free(list->items);
    memset(list, 0, sizeof(*list));
}

static int collect_files(const char *root, const char *relative,
                         PathList *files) {
    char directory_path[4096];
    DIR *directory;
    struct dirent *entry;
    if (snprintf(directory_path, sizeof(directory_path), "%s/%s", root,
                 relative) >= (int)sizeof(directory_path))
        return 0;
    directory = opendir(directory_path);
    if (directory == NULL)
        return 0;
    while ((entry = readdir(directory)) != NULL) {
        char child[4096], path[4096];
        struct stat status;
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0)
            continue;
        if (snprintf(child, sizeof(child), "%s%s%s", relative,
                     relative[0] == '\0' ? "" : "/", entry->d_name) >=
                (int)sizeof(child) ||
            snprintf(path, sizeof(path), "%s/%s", root, child) >=
                (int)sizeof(path) ||
            lstat(path, &status) != 0) {
            closedir(directory);
            return 0;
        }
        if (S_ISDIR(status.st_mode)) {
            if (!collect_files(root, child, files)) {
                closedir(directory);
                return 0;
            }
        } else if (S_ISREG(status.st_mode) && !add_path(files, path)) {
            closedir(directory);
            return 0;
        }
    }
    closedir(directory);
    return 1;
}

static int same_directory_shared_object(const char *path) {
    char directory[4096], base[256], candidate[4096];
    const char *slash = strrchr(path, '/');
    const char *name = slash == NULL ? path : slash + 1;
    size_t length;
    DIR *dir;
    struct dirent *entry;
    if (strlen(name) < 3 || strcmp(name + strlen(name) - 2, ".a") != 0)
        return 0;
    length = strlen(name) - 2;
    if (length >= sizeof(base) || slash == NULL)
        return 0;
    memcpy(base, name, length);
    base[length] = '\0';
    if ((size_t)(slash - path) >= sizeof(directory))
        return 0;
    memcpy(directory, path, (size_t)(slash - path));
    directory[slash - path] = '\0';
    dir = opendir(directory);
    if (dir == NULL)
        return 0;
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, base, length) == 0 &&
            (strcmp(entry->d_name + length, ".so") == 0 ||
             strncmp(entry->d_name + length, ".so.", 4) == 0)) {
            snprintf(candidate, sizeof(candidate), "%s/%s", directory,
                     entry->d_name);
            if (access(candidate, F_OK) == 0) {
                closedir(dir);
                return 1;
            }
        }
    }
    closedir(dir);
    return 0;
}

static int run_objcopy(const char *path) {
    pid_t child = fork();
    int status;
    if (child < 0)
        return 0;
    if (child == 0) {
        execlp("objcopy", "objcopy", "--strip-debug", path, (char *)NULL);
        _exit(127);
    }
    return waitpid(child, &status, 0) >= 0 && WIFEXITED(status) &&
           WEXITSTATUS(status) == 0;
}

static int is_elf(const char *path) {
    unsigned char magic[4];
    int fd = open(path, O_RDONLY | O_NOFOLLOW);
    ssize_t length;
    if (fd < 0)
        return 0;
    length = read(fd, magic, sizeof(magic));
    close(fd);
    return length == (ssize_t)sizeof(magic) && magic[0] == 0x7f &&
           magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F';
}

static int report_prune(CbsExecutionContext *context, const char *type,
                        const char *path, const char *rule,
                        unsigned long long bytes) {
    int result;
    context->current_prune_path = path;
    context->current_prune_rule = rule;
    context->current_prune_bytes = bytes;
    result = cbs_emit_build_event(context, type, NULL, path, rule, 0, 0, 0, 0);
    context->current_prune_path = NULL;
    context->current_prune_rule = NULL;
    context->current_prune_bytes = 0;
    return result;
}

static int apply_rule(const char *root, const char *rule,
                      CbsExecutionContext *context, PathList *files) {
    size_t index;
    size_t matches = 0;
    for (index = 0; index < files->count; ++index) {
        struct stat status;
        const char *path = files->items[index];
        const char *name = strrchr(path, '/');
        name = name == NULL ? path : name + 1;
        if (lstat(path, &status) != 0)
            continue;
        if (strcmp(rule, "strip-debug") == 0) {
            struct stat before = status;
            if (!is_elf(path) || !run_objcopy(path))
                continue;
            if (stat(path, &status) != 0)
                return 0;
            ++matches;
            if (!report_prune(context, "prune-modify", path, rule,
                              before.st_size > status.st_size
                                  ? (unsigned long long)(before.st_size -
                                                         status.st_size)
                                  : 0))
                return 0;
        } else if (strcmp(rule, "drop-libtool-archives") == 0) {
            size_t length = strlen(name);
            if (length < 4 || strcmp(name + length - 3, ".la") != 0)
                continue;
            ++matches;
            if (unlink(path) != 0 ||
                !report_prune(context, "prune-remove", path, rule,
                              (unsigned long long)status.st_size))
                return 0;
        } else if (strcmp(rule, "drop-static-archives") == 0 &&
                   same_directory_shared_object(path)) {
            size_t length = strlen(name);
            if (length < 3 || strcmp(name + length - 2, ".a") != 0)
                continue;
            ++matches;
            if (unlink(path) != 0 ||
                !report_prune(context, "prune-remove", path, rule,
                              (unsigned long long)status.st_size))
                return 0;
        }
    }
    if (matches == 0 && !report_prune(context, "prune-rule", root, rule, 0))
        return 0;
    return 1;
}

int cbs_prune_policy_load(const char *path, CbsPrunePolicy *policy,
                          char *error, size_t error_size) {
    FILE *file;
    char line[128];
    if (policy == NULL || path == NULL)
        return 0;
    memset(policy, 0, sizeof(*policy));
    file = fopen(path, "r");
    if (file == NULL) {
        snprintf(error, error_size, "cannot read prune policy %s: %s", path,
                 strerror(errno));
        return 0;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        char *end = strchr(line, '\n');
        if (end != NULL)
            *end = '\0';
        if (line[0] == '\0' || line[0] == '#')
            continue;
        if (strcmp(line, "strip-debug") == 0)
            policy->strip_debug = 1;
        else if (strcmp(line, "drop-static-archives") == 0)
            policy->drop_static_archives = 1;
        else if (strcmp(line, "drop-libtool-archives") == 0)
            policy->drop_libtool_archives = 1;
        else {
            snprintf(error, error_size, "unknown prune policy rule `%s`",
                     line);
            fclose(file);
            return 0;
        }
    }
    fclose(file);
    return 1;
}

int cbs_prune_staged_tree(const char *root, const CbsPrunePolicy *policy,
                          CbsExecutionContext *context) {
    PathList files = {0};
    int ok = 1;
    if (root == NULL || policy == NULL || context == NULL ||
        !collect_files(root, "", &files))
        return 0;
    if (policy->strip_debug)
        ok = apply_rule(root, "strip-debug", context, &files);
    if (ok && policy->drop_static_archives)
        ok = apply_rule(root, "drop-static-archives", context, &files);
    if (ok && policy->drop_libtool_archives)
        ok = apply_rule(root, "drop-libtool-archives", context, &files);
    free_paths(&files);
    return ok;
}
