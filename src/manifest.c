/* Deterministic staged-tree collection and typed manifest serialization. */
#include "cbs.h"

#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Reject permission bits that would create a privileged installed file. */
static int unsafe_mode(mode_t mode) {
    return (mode & (S_ISUID | S_ISGID)) != 0;
}

static int allowance_matches(const char *path, mode_t mode,
                             const CbsPrivilegedAllowance *allowances,
                             size_t allowance_count) {
    size_t index;
    for (index = 0; index < allowance_count; ++index)
        if (strcmp(path, allowances[index].path) == 0 &&
            (unsigned)(mode & 07777) == allowances[index].mode)
            return 1;
    return 0;
}

static void set_error(char *error, size_t error_size, const char *format, ...) {
    va_list arguments;
    if (error == NULL || error_size == 0)
        return;
    va_start(arguments, format);
    vsnprintf(error, error_size, format, arguments);
    va_end(arguments);
}

/* Check that a relative staged symlink stays within the staged root. */
static int safe_link_target(const char *relative, const char *target) {
    const char *p;
    int depth = 0;
    if (target[0] == '/')
        return 1;
    for (p = relative; *p != '\0'; ++p)
        if (*p == '/')
            ++depth;
    for (p = target; *p != '\0';) {
        const char *start;
        size_t length;
        while (*p == '/')
            ++p;
        start = p;
        while (*p != '\0' && *p != '/')
            ++p;
        length = (size_t)(p - start);
        if (length == 0 || (length == 1 && start[0] == '.'))
            continue;
        if (length == 2 && start[0] == '.' && start[1] == '.') {
            if (depth == 0)
                return 0;
            --depth;
        } else
            ++depth;
    }
    return 1;
}

/* Append one validated entry while growing the manifest array. */
static int add_entry(CbsManifestEntry **items, size_t *count, size_t *capacity,
                     const char *path, char type, mode_t mode,
                     const char *target,
                     const CbsPrivilegedAllowance *allowances,
                     size_t allowance_count, char *error, size_t error_size) {
    CbsManifestEntry *entry;
    if (unsafe_mode(mode) &&
        !allowance_matches(path, mode, allowances, allowance_count)) {
        set_error(error, error_size,
                  "%s has mode %04o; CIXPKG refuses setuid and setgid",
                  path, (unsigned)(mode & 07777));
        return 0;
    }
    if (*count == *capacity) {
        size_t next = *capacity == 0 ? 16 : *capacity * 2;
        CbsManifestEntry *grown = realloc(*items, next * sizeof(*grown));
        if (grown == NULL) {
            set_error(error, error_size,
                      "%s could not be recorded: out of memory", path);
            return 0;
        }
        *items = grown;
        *capacity = next;
    }
    entry = &(*items)[(*count)++];
    memset(entry, 0, sizeof(*entry));
    entry->path = cbs_duplicate(path);
    entry->type = type;
    entry->mode = (unsigned)(mode & 07777);
    entry->uid = 0;
    entry->gid = 0;
    if (target != NULL)
        entry->target = cbs_duplicate(target);
    return 1;
}

/* Recursively collect supported entries from a staged directory. */
static int collect(const char *root, const char *relative,
                   CbsManifestEntry **items, size_t *count, size_t *capacity,
                   const CbsPrivilegedAllowance *allowances,
                   size_t allowance_count, char *error, size_t error_size) {
    char path[4096];
    DIR *directory;
    struct dirent *entry;
    if (snprintf(path, sizeof(path), "%s/%s", root, relative) >=
        (int)sizeof(path)) {
        set_error(error, error_size, "%s: path is too long", relative);
        return 0;
    }
    directory = opendir(path);
    if (directory == NULL) {
        set_error(error, error_size, "%s: cannot open directory: %s",
                  relative[0] ? relative : ".", strerror(errno));
        return 0;
    }
    while ((entry = readdir(directory)) != NULL) {
        char child[4096];
        struct stat status;
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;
        if (snprintf(child, sizeof(child), "%s%s%s", relative,
                     relative[0] ? "/" : "", entry->d_name) >=
            (int)sizeof(child)) {
            set_error(error, error_size, "%s/%s: path is too long", relative,
                      entry->d_name);
            closedir(directory);
            return 0;
        }
        if (snprintf(path, sizeof(path), "%s/%s", root, child) >=
                (int)sizeof(path) ||
            lstat(path, &status) != 0) {
            set_error(error, error_size, "%s: cannot inspect staged entry: %s",
                      child, strerror(errno));
            closedir(directory);
            return 0;
        }
        if (S_ISDIR(status.st_mode)) {
            if (!add_entry(items, count, capacity, child, 'd', status.st_mode,
                           NULL, allowances, allowance_count, error, error_size) ||
                !collect(root, child, items, count, capacity, allowances,
                         allowance_count, error, error_size)) {
                closedir(directory);
                return 0;
            }
        } else if (S_ISREG(status.st_mode)) {
            char digest[65];
            if (status.st_nlink != 1) {
                set_error(error, error_size,
                          "%s is a hard link (link count %lu); CIXPKG "
                          "entries must have exactly one link",
                          child, (unsigned long)status.st_nlink);
                closedir(directory);
                return 0;
            }
            if (!add_entry(items, count, capacity, child, 'f', status.st_mode,
                           NULL, allowances, allowance_count, error, error_size) ||
                !cbs_digest_file(path, digest)) {
                if (error == NULL || error[0] == '\0')
                    set_error(error, error_size,
                              "%s: cannot read file for digest", child);
                closedir(directory);
                return 0;
            }
            (*items)[*count - 1].size = (unsigned long long)status.st_size;
            (*items)[*count - 1].digest = cbs_duplicate(digest);
        } else if (S_ISLNK(status.st_mode)) {
            char target[4096];
            ssize_t length = readlink(path, target, sizeof(target) - 1);
            if (length < 0 || (size_t)length >= sizeof(target) - 1) {
                closedir(directory);
                return 0;
            }
            target[length] = '\0';
            if (!safe_link_target(child, target)) {
                set_error(error, error_size,
                          "%s -> %s escapes the staged root; CIXPKG "
                          "symlink targets must be relative and contained",
                          child, target);
                closedir(directory);
                return 0;
            }
            if (!add_entry(items, count, capacity, child, 'l', status.st_mode,
                           target, allowances, allowance_count, error, error_size)) {
                closedir(directory);
                return 0;
            }
        } else {
            set_error(error, error_size,
                      "%s is an unsupported file type; CIXPKG carries "
                      "directories, regular files and symlinks only", child);
            closedir(directory);
            return 0;
        }
    }
    closedir(directory);
    return 1;
}

/* Encode a symlink target as lowercase hexadecimal bytes. */
static int write_target(FILE *file, const char *target) {
    static const char hex[] = "0123456789abcdef";
    const unsigned char *cursor = (const unsigned char *)target;
    while (*cursor) {
        if (fprintf(file, "%c%c", hex[*cursor >> 4], hex[*cursor & 15]) < 0)
            return 0;
        ++cursor;
    }
    return 1;
}

/* Serialize one typed manifest entry. */
static int write_entry(FILE *file, const CbsManifestEntry *entry) {
    if (entry->type == 'f')
        return fprintf(file, "f %o 0 0 %llu %s %s\n", entry->mode, entry->size,
                       entry->digest, entry->path) >= 0;
    if (entry->type == 'd')
        return fprintf(file, "d %o 0 0 %s\n", entry->mode, entry->path) >= 0;
    return fprintf(file, "l %o 0 0 ", entry->mode) >= 0 &&
           write_target(file, entry->target) &&
           fprintf(file, " %s\n", entry->path) >= 0;
}

/* Collect, sort, and write a deterministic manifest file. */
int cbs_manifest_write_with_license_policy_error(
    const char *root, const char *output, const char *license,
    const CbsPrivilegedAllowance *allowances, size_t allowance_count,
    char *error, size_t error_size) {
    CbsManifestEntry *items = NULL;
    size_t count = 0, capacity = 0, index;
    FILE *file;
    if (error != NULL && error_size > 0)
        error[0] = '\0';
    if (!collect(root, "", &items, &count, &capacity, allowances,
                 allowance_count, error, error_size))
        goto fail;
    qsort(items, count, sizeof(*items), cbs_manifest_compare);
    file = fopen(output, "wb");
    if (file == NULL) {
        set_error(error, error_size, "%s: cannot write manifest: %s", output,
                  strerror(errno));
        goto fail;
    }
    if (license != NULL && fprintf(file, "m license %s\n", license) < 0) {
        fclose(file);
        goto fail;
    }
    for (index = 0; index < count; ++index)
        if (!write_entry(file, &items[index])) {
            fclose(file);
            goto fail;
        }
    if (fclose(file) != 0)
        goto fail;
    cbs_manifest_entries_destroy(items, count);
    return 1;
fail:
    cbs_manifest_entries_destroy(items, count);
    return 0;
}

int cbs_manifest_write_with_license_error(const char *root, const char *output,
                                          const char *license, char *error,
                                          size_t error_size) {
    return cbs_manifest_write_with_license_policy_error(
        root, output, license, NULL, 0, error, error_size);
}

int cbs_manifest_write_with_license(const char *root, const char *output,
                                    const char *license) {
    return cbs_manifest_write_with_license_error(root, output, license, NULL,
                                                 0);
}

int cbs_manifest_write(const char *root, const char *output) {
    return cbs_manifest_write_with_license(root, output, NULL);
}

int cbs_manifest_compare(const void *left, const void *right) {
    const CbsManifestEntry *a = left, *b = right;
    return strcmp(a->path, b->path);
}

/* Return sorted manifest entries for callers that need direct inspection. */
int cbs_manifest_collect(const char *root, CbsManifestEntry **entries,
                         size_t *count) {
    return cbs_manifest_collect_with_policy_error(root, entries, count, NULL,
                                                  0, NULL, 0);
}

int cbs_manifest_collect_with_error(const char *root,
                                    CbsManifestEntry **entries,
                                    size_t *count, char *error,
                                    size_t error_size) {
    return cbs_manifest_collect_with_policy_error(
        root, entries, count, NULL, 0, error, error_size);
}

int cbs_manifest_collect_with_policy_error(
    const char *root, CbsManifestEntry **entries, size_t *count,
    const CbsPrivilegedAllowance *allowances, size_t allowance_count,
    char *error, size_t error_size) {
    size_t capacity = 0;
    if (!root || !entries || !count)
        return 0;
    *entries = NULL;
    *count = 0;
    if (error != NULL && error_size > 0)
        error[0] = '\0';
    if (!collect(root, "", entries, count, &capacity, allowances,
                 allowance_count, error, error_size)) {
        cbs_manifest_entries_destroy(*entries, *count);
        *entries = NULL;
        *count = 0;
        return 0;
    }
    qsort(*entries, *count, sizeof(**entries), cbs_manifest_compare);
    return 1;
}

/* Release entries and all strings owned by the manifest collector. */
void cbs_manifest_entries_destroy(CbsManifestEntry *entries, size_t count) {
    size_t index;
    if (!entries)
        return;
    for (index = 0; index < count; ++index) {
        free((char *)entries[index].path);
        free((char *)entries[index].digest);
        free((char *)entries[index].target);
    }
    free(entries);
}
