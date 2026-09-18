/* Safe source-archive inspection and extraction through libarchive. */
#include "cbs.h"

#include <archive.h>
#include <archive_entry.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* A directory member whose mode and mtime are applied after every file has
 * been written, so read-only or late directory entries cannot block writes. */
typedef struct {
    char *name;
    char *path;
    mode_t mode;
    struct timespec time;
} DirectoryEntry;
/* Return false when an archive member can escape its extraction root. */
static int safe_name(const char *name) {
    const char *part = name;
    if (name == NULL || name[0] == '/' || strstr(name, "\\") != NULL)
        return 0;
    while (*part != '\0') {
        const char *end = strchr(part, '/');
        size_t length = end == NULL ? strlen(part) : (size_t)(end - part);
        if (length == 2 && strncmp(part, "..", 2) == 0)
            return 0;
        part = end == NULL ? part + length : end + 1;
    }
    return 1;
}

/* Accept only archive formats supported by the package policy. */
static int supported_format(const char *name) {
    return name != NULL &&
           (strstr(name, "tar") != NULL || strstr(name, "pax") != NULL ||
            strstr(name, "ZIP") != NULL);
}

/* A link target is interpreted relative to the archive member's directory. */
static int safe_link_target(const char *name, const char *target) {
    const char *part;
    int depth = 0;

    if (name == NULL || target == NULL || target[0] == '/' ||
        strstr(target, "\\") != NULL)
        return 0;
    part = name;
    while (*part != '\0') {
        const char *end = strchr(part, '/');
        size_t length = end == NULL ? strlen(part) : (size_t)(end - part);
        if (end == NULL)
            break; /* the final component is the link itself */
        if (length == 2 && strncmp(part, "..", 2) == 0) {
            if (depth == 0)
                return 0;
            --depth;
        } else if (length != 0 && !(length == 1 && part[0] == '.')) {
            ++depth;
        }
        part = end + 1;
    }
    part = target;
    while (*part != '\0') {
        const char *end = strchr(part, '/');
        size_t length = end == NULL ? strlen(part) : (size_t)(end - part);
        if (length == 2 && strncmp(part, "..", 2) == 0) {
            if (depth == 0)
                return 0;
            --depth;
        } else if (length != 0 && !(length == 1 && part[0] == '.')) {
            ++depth;
        }
        part = end == NULL ? part + length : end + 1;
    }
    return 1;
}

/* Existing symlink components must never be followed while extracting. */
static int has_symlink_component(const char *path, const char *destination) {
    char copy[4096];
    size_t destination_length;
    char *cursor;
    struct stat status;

    if (snprintf(copy, sizeof(copy), "%s", path) >= (int)sizeof(copy))
        return 1;
    destination_length = strlen(destination);
    if (strncmp(copy, destination, destination_length) != 0 ||
        (copy[destination_length] != '/' && copy[destination_length] != '\0'))
        return 1;
    cursor = copy + destination_length;
    while (*cursor == '/')
        ++cursor;
    while (*cursor != '\0') {
        char *slash = strchr(cursor, '/');
        if (slash != NULL)
            *slash = '\0';
        if (lstat(copy, &status) == 0 && S_ISLNK(status.st_mode))
            return 1;
        if (slash == NULL)
            break;
        *slash = '/';
        cursor = slash + 1;
        while (*cursor == '/')
            ++cursor;
    }
    return 0;
}

/* Create every missing parent directory of an extraction path. Callers have
 * already checked that no existing component is a symbolic link. */
static int create_parents(const char *path, const char *destination) {
    char copy[4096];
    char *cursor;
    if (snprintf(copy, sizeof(copy), "%s", path) >= (int)sizeof(copy))
        return 0;
    cursor = copy + strlen(destination);
    while (*cursor == '/')
        ++cursor;
    while ((cursor = strchr(cursor, '/')) != NULL) {
        *cursor = '\0';
        if (mkdir(copy, 0755) != 0 && errno != EEXIST)
            return 0;
        *cursor = '/';
        while (*cursor == '/')
            ++cursor;
    }
    return 1;
}

/* Record a directory member for the deferred mode and timestamp pass. */
static void remember_directory(DirectoryEntry **directories, size_t *count,
                              size_t *capacity, const char *name,
                              const char *path, mode_t mode,
                              struct archive_entry *entry) {
    DirectoryEntry *directory;
    if (*count == *capacity) {
        *capacity = *capacity == 0 ? 8 : *capacity * 2;
        *directories =
            cbs_reallocate(*directories, *capacity * sizeof(**directories));
    }
    directory = &(*directories)[(*count)++];
    directory->name = cbs_duplicate(name);
    directory->path = cbs_duplicate(path);
    directory->mode = mode & 07777;
    directory->time.tv_sec = archive_entry_mtime(entry);
    directory->time.tv_nsec = archive_entry_mtime_nsec(entry);
}

/* Every rejection names the member and the rule so the report can be read
 * without guessing. `rule` is a policy sentence; `failure_errno` is set only
 * when a filesystem operation failed. */
#define REJECT(why)                                                          \
    do {                                                                     \
        rule = (why);                                                        \
        failure_errno = 0;                                                   \
        goto fail;                                                           \
    } while (0)
#define FAIL_OPERATION(what)                                                 \
    do {                                                                     \
        failure_errno = errno;                                               \
        rule = (what);                                                       \
        goto fail;                                                           \
    } while (0)

/* Extract ordinary members first, then links so links cannot redirect writes.
 * Parent directories missing from the archive are created on demand; archive
 * directory members get their mode and mtime after all members are written. */
int cbs_extract_archive(const char *archive_path, const char *destination,
                        const char *source_name, const char *recipe_path,
                        const char *recipe_source, CbsLocation location) {
    struct archive *reader = archive_read_new();
    struct archive_entry *entry = NULL;
    int result = 0;
    int header_result;
    struct stat status;
    char path[4096], rule_text[4352];
    const char *member = NULL;
    const char *rule = NULL;
    int failure_errno = 0;
    DirectoryEntry *directories = NULL;
    size_t directory_count = 0;
    size_t directory_capacity = 0;
    if (reader == NULL)
        return 0;
    archive_read_support_filter_all(reader);
    archive_read_support_format_all(reader);
    for (int pass = 0; pass < 2; ++pass) {
        member = NULL;
        if (pass != 0) {
            archive_read_free(reader);
            reader = archive_read_new();
            if (reader == NULL)
                REJECT("cannot allocate an archive reader");
            archive_read_support_filter_all(reader);
            archive_read_support_format_all(reader);
        }
        if (archive_read_open_filename(reader, archive_path, 65536) != ARCHIVE_OK)
            REJECT("cannot open archive");
        header_result = archive_read_next_header(reader, &entry);
        if (header_result == ARCHIVE_EOF)
            REJECT("archive contains no members");
        if (header_result != ARCHIVE_OK)
            REJECT("cannot read archive header");
        if (!supported_format(archive_format_name(reader))) {
            const char *format = archive_format_name(reader);
            snprintf(rule_text, sizeof(rule_text),
                     "archive format `%s` is not tar, pax, or zip",
                     format == NULL ? "unknown" : format);
            REJECT(rule_text);
        }
        do {
        const char *name = archive_entry_pathname(entry);
        const char *symlink_target = archive_entry_symlink(entry);
        const char *hardlink = archive_entry_hardlink(entry);
        mode_t mode = archive_entry_mode(entry);
        struct timespec times[2];
        int fd;
        int is_link = archive_entry_filetype(entry) == AE_IFLNK ||
                      hardlink != NULL;
        member = name;
        if (!safe_name(name))
            REJECT("unsafe path: absolute, backslash, or `..` component");
        if (archive_entry_filetype(entry) == AE_IFCHR)
            REJECT("character device");
        if (archive_entry_filetype(entry) == AE_IFBLK)
            REJECT("block device");
        if (archive_entry_filetype(entry) == AE_IFIFO)
            REJECT("FIFO");
        if (is_link && symlink_target != NULL &&
            !safe_link_target(name, symlink_target)) {
            snprintf(rule_text, sizeof(rule_text),
                     "symbolic link target `%s` leaves the archive root",
                     symlink_target);
            REJECT(rule_text);
        }
        if (is_link && symlink_target == NULL && !safe_name(hardlink)) {
            snprintf(rule_text, sizeof(rule_text),
                     "hard link target `%s` leaves the archive root",
                     hardlink == NULL ? "" : hardlink);
            REJECT(rule_text);
        }
        if (snprintf(path, sizeof(path), "%s/%s", destination, name) >=
            (int)sizeof(path))
            REJECT("path is too long");
        if (has_symlink_component(path, destination))
            REJECT("path crosses an existing symbolic link");
        if (pass == 0 && is_link)
            continue;
        if (!create_parents(path, destination))
            FAIL_OPERATION("cannot create parent directory");
        if (pass == 0 && archive_entry_filetype(entry) == AE_IFDIR) {
            if (mkdir(path, 0755) != 0 && errno != EEXIST)
                FAIL_OPERATION("cannot create directory");
            remember_directory(&directories, &directory_count,
                               &directory_capacity, name, path, mode, entry);
        } else if (pass == 0) {
            fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, mode & 07777);
            if (fd < 0)
                FAIL_OPERATION("cannot create file");
            if (archive_read_data_into_fd(reader, fd) != ARCHIVE_OK) {
                close(fd);
                REJECT("cannot write file data");
            }
            close(fd);
            times[0].tv_sec = times[1].tv_sec = archive_entry_mtime(entry);
            times[0].tv_nsec = times[1].tv_nsec =
                archive_entry_mtime_nsec(entry);
            if (utimensat(AT_FDCWD, path, times, 0) != 0)
                FAIL_OPERATION("cannot set file timestamps");
        } else if (symlink_target != NULL) {
            if (symlink(symlink_target, path) != 0)
                FAIL_OPERATION("cannot create symbolic link");
            times[0].tv_sec = times[1].tv_sec = archive_entry_mtime(entry);
            times[0].tv_nsec = times[1].tv_nsec =
                archive_entry_mtime_nsec(entry);
            if (utimensat(AT_FDCWD, path, times, AT_SYMLINK_NOFOLLOW) != 0)
                FAIL_OPERATION("cannot set symbolic link timestamps");
        } else if (hardlink != NULL) {
            char target_path[4096];
            if (snprintf(target_path, sizeof(target_path), "%s/%s",
                         destination, hardlink) >= (int)sizeof(target_path))
                REJECT("hard link target path is too long");
            if (lstat(target_path, &status) != 0)
                FAIL_OPERATION("cannot find hard link target");
            if (!S_ISREG(status.st_mode))
                REJECT("hard link target is not a regular file");
            if (link(target_path, path) != 0)
                FAIL_OPERATION("cannot create hard link");
            times[0].tv_sec = times[1].tv_sec = archive_entry_mtime(entry);
            times[0].tv_nsec = times[1].tv_nsec =
                archive_entry_mtime_nsec(entry);
            if (utimensat(AT_FDCWD, path, times, 0) != 0)
                FAIL_OPERATION("cannot set hard link timestamps");
        }
        } while ((header_result = archive_read_next_header(reader, &entry)) ==
                 ARCHIVE_OK);
        member = NULL;
        if (header_result != ARCHIVE_EOF)
            REJECT("cannot read archive header");
        archive_read_close(reader);
    }
    for (size_t index = 0; index < directory_count; ++index) {
        struct timespec times[2] = {directories[index].time,
                                    directories[index].time};
        member = directories[index].name;
        if (chmod(directories[index].path, directories[index].mode) != 0)
            FAIL_OPERATION("cannot set directory mode");
        if (utimensat(AT_FDCWD, directories[index].path, times, 0) != 0)
            FAIL_OPERATION("cannot set directory timestamps");
    }
    result = 1;
fail:
    if (!result) {
        const char *library_error =
            reader == NULL ? NULL : archive_error_string(reader);
        const char *detail = failure_errno != 0 ? strerror(failure_errno)
                             : library_error;
        char message[4096 + 4352 + 256];
        if (member != NULL)
            snprintf(message, sizeof(message),
                     "source `%s`: member \"%s\": %s%s%s%s",
                     source_name == NULL ? "unknown" : source_name, member,
                     failure_errno != 0 || library_error != NULL
                         ? ""
                         : "rejected: ",
                     rule == NULL ? "extraction failed" : rule,
                     detail == NULL ? "" : ": ", detail == NULL ? "" : detail);
        else
            snprintf(message, sizeof(message), "source `%s`: %s%s%s",
                     source_name == NULL ? "unknown" : source_name,
                     rule == NULL ? "extraction failed" : rule,
                     detail == NULL ? "" : ": ", detail == NULL ? "" : detail);
        cbs_diagnostic(recipe_path, recipe_source, location, "error",
                       "CPDL-E6001", CBS_DIAG_SOURCE, message);
    }
    if (reader != NULL)
        archive_read_free(reader);
    for (size_t index = 0; index < directory_count; ++index) {
        free(directories[index].name);
        free(directories[index].path);
    }
    free(directories);
    return result;
}
#undef REJECT
#undef FAIL_OPERATION
