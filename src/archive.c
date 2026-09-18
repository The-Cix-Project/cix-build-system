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

typedef struct {
    char *path;
    struct timespec time;
} DirectoryTime;

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

/* Extract ordinary members first, then links so links cannot redirect writes. */
int cbs_extract_archive(const char *archive_path, const char *destination,
                        const char *source_name, const char *recipe_path,
                        const char *recipe_source, CbsLocation location) {
    struct archive *reader = archive_read_new();
    struct archive_entry *entry;
    int result = 0;
    int header_result;
    struct stat status;
    char path[4096], message[512];
    DirectoryTime *directories = NULL;
    size_t directory_count = 0;
    size_t directory_capacity = 0;
    if (reader == NULL)
        return 0;
    archive_read_support_filter_all(reader);
    archive_read_support_format_all(reader);
    for (int pass = 0; pass < 2; ++pass) {
        if (pass != 0) {
            archive_read_free(reader);
            reader = archive_read_new();
            if (reader == NULL)
                goto fail;
            archive_read_support_filter_all(reader);
            archive_read_support_format_all(reader);
        }
        if (archive_read_open_filename(reader, archive_path, 65536) != ARCHIVE_OK)
            goto fail;
        if (archive_read_next_header(reader, &entry) != ARCHIVE_OK)
            goto fail;
        if (!supported_format(archive_format_name(reader)))
            goto fail;
        do {
        const char *name = archive_entry_pathname(entry);
        const char *symlink_target = archive_entry_symlink(entry);
        const char *hardlink = archive_entry_hardlink(entry);
        mode_t mode = archive_entry_mode(entry);
        struct timespec entry_time;
        int fd;
        int is_link = archive_entry_filetype(entry) == AE_IFLNK ||
                      hardlink != NULL;
        if (!safe_name(name) ||
            archive_entry_filetype(entry) == AE_IFCHR ||
            archive_entry_filetype(entry) == AE_IFBLK ||
            archive_entry_filetype(entry) == AE_IFIFO ||
            (is_link && (symlink_target != NULL
                             ? !safe_link_target(name, symlink_target)
                             : !safe_name(hardlink))))
            goto fail;
        if (snprintf(path, sizeof(path), "%s/%s", destination, name) >=
            (int)sizeof(path))
            goto fail;
        if (has_symlink_component(path, destination))
            goto fail;
        if (pass == 0 && is_link)
            continue;
        if (pass == 0 && archive_entry_filetype(entry) == AE_IFDIR) {
            if (mkdir(path, mode & 07777) != 0 && errno != EEXIST)
                goto fail;
            if (directory_count == directory_capacity) {
                directory_capacity = directory_capacity == 0
                                         ? 8
                                         : directory_capacity * 2;
                directories = cbs_reallocate(
                    directories, directory_capacity * sizeof(*directories));
            }
            directories[directory_count].path = cbs_duplicate(path);
            directories[directory_count].time.tv_sec = archive_entry_mtime(entry);
            directories[directory_count].time.tv_nsec =
                archive_entry_mtime_nsec(entry);
            ++directory_count;
        } else if (pass == 0) {
            struct timespec times[2];
            fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, mode & 07777);
            if (fd < 0 || archive_read_data_into_fd(reader, fd) != ARCHIVE_OK) {
                if (fd >= 0)
                    close(fd);
                goto fail;
            }
            close(fd);
            entry_time.tv_sec = archive_entry_mtime(entry);
            entry_time.tv_nsec = archive_entry_mtime_nsec(entry);
            times[0] = entry_time;
            times[1] = entry_time;
            if (utimensat(AT_FDCWD, path, times, 0) != 0)
                goto fail;
        } else if (symlink_target != NULL) {
            struct timespec times[2];
            if (symlink(symlink_target, path) != 0)
                goto fail;
            entry_time.tv_sec = archive_entry_mtime(entry);
            entry_time.tv_nsec = archive_entry_mtime_nsec(entry);
            times[0] = entry_time;
            times[1] = entry_time;
            if (utimensat(AT_FDCWD, path, times, AT_SYMLINK_NOFOLLOW) != 0)
                goto fail;
        } else if (hardlink != NULL) {
            char target_path[4096];
            struct timespec times[2];
            if (snprintf(target_path, sizeof(target_path), "%s/%s",
                         destination, hardlink) >= (int)sizeof(target_path) ||
                lstat(target_path, &status) != 0 || !S_ISREG(status.st_mode) ||
                link(target_path, path) != 0)
                goto fail;
            entry_time.tv_sec = archive_entry_mtime(entry);
            entry_time.tv_nsec = archive_entry_mtime_nsec(entry);
            times[0] = entry_time;
            times[1] = entry_time;
            if (utimensat(AT_FDCWD, path, times, 0) != 0)
                goto fail;
        }
        } while ((header_result = archive_read_next_header(reader, &entry)) ==
                 ARCHIVE_OK);
        if (header_result != ARCHIVE_EOF)
            goto fail;
        archive_read_close(reader);
    }
    for (size_t index = 0; index < directory_count; ++index) {
        struct timespec times[2] = {directories[index].time,
                                    directories[index].time};
        if (utimensat(AT_FDCWD, directories[index].path, times, 0) != 0)
            goto fail;
    }
    result = 1;
fail:
    if (!result) {
        snprintf(message, sizeof(message),
                 "source `%s` archive format or entry is unsupported: %s",
                 source_name == NULL ? "unknown" : source_name,
                 archive_error_string(reader) == NULL
                     ? "unsafe archive"
                     : archive_error_string(reader));
        cbs_diagnostic(recipe_path, recipe_source, location, "error",
                       "CPDL-E6001", CBS_DIAG_SOURCE, message);
    }
    if (reader != NULL)
        archive_read_free(reader);
    for (size_t index = 0; index < directory_count; ++index)
        free(directories[index].path);
    free(directories);
    return result;
}
