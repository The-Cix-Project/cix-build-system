#include "cbs.h"

#include <archive.h>
#include <archive_entry.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int safe_name(const char *name)
{
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

static int supported_format(const char *name)
{
    return name != NULL && (strstr(name, "tar") != NULL ||
                            strstr(name, "ZIP") != NULL);
}

int cbs_extract_archive(const char *archive_path, const char *destination,
                        const char *source_name, const char *recipe_path,
                        const char *recipe_source, CbsLocation location)
{
    struct archive *reader = archive_read_new();
    struct archive_entry *entry;
    int result = 0;
    char path[4096], message[512];
    if (reader == NULL) return 0;
    archive_read_support_filter_all(reader);
    archive_read_support_format_all(reader);
    if (archive_read_open_filename(reader, archive_path, 65536) != ARCHIVE_OK)
        goto fail;
    if (archive_read_next_header(reader, &entry) != ARCHIVE_OK) goto fail;
    if (!supported_format(archive_format_name(reader))) goto fail;
    do {
        const char *name = archive_entry_pathname(entry);
        mode_t mode = archive_entry_mode(entry);
        int fd;
        if (!safe_name(name) || archive_entry_filetype(entry) == AE_IFLNK ||
            archive_entry_filetype(entry) == AE_IFCHR ||
            archive_entry_filetype(entry) == AE_IFBLK ||
            archive_entry_filetype(entry) == AE_IFIFO ||
            archive_entry_hardlink(entry) != NULL) goto fail;
        if (snprintf(path, sizeof(path), "%s/%s", destination, name) >=
            (int)sizeof(path)) goto fail;
        if (archive_entry_filetype(entry) == AE_IFDIR) {
            if (mkdir(path, mode & 07777) != 0 && errno != EEXIST) goto fail;
        } else {
            fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, mode & 07777);
            if (fd < 0 || archive_read_data_into_fd(reader, fd) != ARCHIVE_OK) {
                if (fd >= 0) close(fd);
                goto fail;
            }
            close(fd);
        }
    } while (archive_read_next_header(reader, &entry) == ARCHIVE_OK);
    result = 1;
fail:
    if (!result) {
        snprintf(message, sizeof(message), "source `%s` archive format or entry is unsupported: %s",
                 source_name == NULL ? "unknown" : source_name,
                 archive_error_string(reader) == NULL ? "unsafe archive" : archive_error_string(reader));
        cbs_diagnostic(recipe_path, recipe_source, location, "error",
                       "CPDL-E6001", CBS_DIAG_SOURCE, message);
    }
    archive_read_free(reader);
    return result;
}
