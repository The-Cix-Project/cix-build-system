#define _POSIX_C_SOURCE 200809L
/* Regression tests for archive format, traversal, link rejection, implicit
 * parent directories, deferred directory metadata, and named diagnostics. */
#include "cbs.h"
#include <archive.h>
#include <archive_entry.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int make_unsafe_archive(const char *path, int symlink) {
    struct archive *archive = archive_write_new();
    struct archive_entry *entry;
    int ok = 0;

    if (archive == NULL ||
        archive_write_set_format_pax_restricted(archive) != ARCHIVE_OK ||
        archive_write_open_filename(archive, path) != ARCHIVE_OK)
        goto done;
    entry = archive_entry_new();
    archive_entry_set_pathname(entry, symlink ? "link" : "../escape");
    archive_entry_set_filetype(entry, symlink ? AE_IFLNK : AE_IFREG);
    archive_entry_set_perm(entry, 0644);
    if (symlink)
        archive_entry_set_symlink(entry, "/outside");
    else {
        const char byte = 'x';
        archive_entry_set_size(entry, 1);
        if (archive_write_header(archive, entry) != ARCHIVE_OK ||
            archive_write_data(archive, &byte, 1) != 1)
            goto done_entry;
        ok = 1;
        goto done_entry;
    }
    ok = archive_write_header(archive, entry) == ARCHIVE_OK;
done_entry:
    archive_entry_free(entry);
done:
    if (archive != NULL) {
        archive_write_close(archive);
        archive_write_free(archive);
    }
    return ok;
}

static int make_link_archive(const char *path, int unsafe) {
    struct archive *archive = archive_write_new();
    struct archive_entry *entry = NULL;
    const char byte = 'x';
    int ok = 0;

    if (archive == NULL ||
        archive_write_set_format_pax_restricted(archive) != ARCHIVE_OK ||
        archive_write_open_filename(archive, path) != ARCHIVE_OK)
        goto done;
    entry = archive_entry_new();
    archive_entry_set_pathname(entry, "target");
    archive_entry_set_filetype(entry, AE_IFREG);
    archive_entry_set_perm(entry, 0644);
    archive_entry_set_size(entry, 1);
    if (archive_write_header(archive, entry) != ARCHIVE_OK ||
        archive_write_data(archive, &byte, 1) != 1)
        goto done_entry;
    archive_entry_clear(entry);
    archive_entry_set_pathname(entry, "dir");
    archive_entry_set_filetype(entry, AE_IFDIR);
    archive_entry_set_perm(entry, 0755);
    if (archive_write_header(archive, entry) != ARCHIVE_OK)
        goto done_entry;
    archive_entry_clear(entry);
    archive_entry_set_pathname(entry, "dir/link");
    archive_entry_set_filetype(entry, AE_IFLNK);
    archive_entry_set_symlink(entry, unsafe ? "../../outside" : "../target");
    archive_entry_set_perm(entry, 0777);
    if (archive_write_header(archive, entry) != ARCHIVE_OK)
        goto done_entry;
    if (unsafe)
        ok = 1;
    else {
        archive_entry_clear(entry);
        archive_entry_set_pathname(entry, "hard");
        archive_entry_set_filetype(entry, AE_IFREG);
        archive_entry_set_hardlink(entry, "target");
        if (archive_write_header(archive, entry) != ARCHIVE_OK)
            goto done_entry;
        ok = 1;
    }
done_entry:
    archive_entry_free(entry);
done:
    if (archive != NULL) {
        archive_write_close(archive);
        archive_write_free(archive);
    }
    return ok;
}

static int make_timestamp_archive(const char *path) {
    struct archive *archive = archive_write_new();
    struct archive_entry *entry = NULL;
    const char byte = 'x';
    int ok = 0;
    if (archive == NULL ||
        archive_write_set_format_pax_restricted(archive) != ARCHIVE_OK ||
        archive_write_open_filename(archive, path) != ARCHIVE_OK)
        goto done;
    entry = archive_entry_new();
    archive_entry_set_pathname(entry, "tree");
    archive_entry_set_filetype(entry, AE_IFDIR);
    archive_entry_set_perm(entry, 0755);
    archive_entry_set_mtime(entry, 100, 0);
    if (archive_write_header(archive, entry) != ARCHIVE_OK)
        goto done_entry;
    archive_entry_clear(entry);
    archive_entry_set_pathname(entry, "tree/early");
    archive_entry_set_filetype(entry, AE_IFREG);
    archive_entry_set_perm(entry, 0644);
    archive_entry_set_size(entry, 1);
    archive_entry_set_mtime(entry, 200, 0);
    if (archive_write_header(archive, entry) != ARCHIVE_OK ||
        archive_write_data(archive, &byte, 1) != 1)
        goto done_entry;
    archive_entry_clear(entry);
    archive_entry_set_pathname(entry, "tree/late");
    archive_entry_set_filetype(entry, AE_IFREG);
    archive_entry_set_perm(entry, 0644);
    archive_entry_set_size(entry, 1);
    archive_entry_set_mtime(entry, 300, 0);
    if (archive_write_header(archive, entry) != ARCHIVE_OK ||
        archive_write_data(archive, &byte, 1) != 1)
        goto done_entry;
    archive_entry_clear(entry);
    archive_entry_set_pathname(entry, "tree/link");
    archive_entry_set_filetype(entry, AE_IFLNK);
    archive_entry_set_perm(entry, 0777);
    archive_entry_set_symlink(entry, "early");
    archive_entry_set_mtime(entry, 400, 0);
    if (archive_write_header(archive, entry) != ARCHIVE_OK)
        goto done_entry;
    ok = 1;
done_entry:
    archive_entry_free(entry);
done:
    if (archive != NULL) {
        archive_write_close(archive);
        archive_write_free(archive);
    }
    return ok;
}

/* An archive shaped like bzip2-1.0.8.tar.gz: files under directories that
 * have no member of their own, a directory holding only a symlink, a
 * directory member that arrives after its contents, and a read-only
 * directory member that arrives before its contents. */
static int make_implicit_archive(const char *path) {
    struct archive *archive = archive_write_new();
    struct archive_entry *entry = NULL;
    const char byte = 'x';
    int ok = 0;
    if (archive == NULL ||
        archive_write_set_format_pax_restricted(archive) != ARCHIVE_OK ||
        archive_write_open_filename(archive, path) != ARCHIVE_OK)
        goto done;
    entry = archive_entry_new();
    archive_entry_set_pathname(entry, "pkg-1.0/lib/file.c");
    archive_entry_set_filetype(entry, AE_IFREG);
    archive_entry_set_perm(entry, 0644);
    archive_entry_set_size(entry, 1);
    if (archive_write_header(archive, entry) != ARCHIVE_OK ||
        archive_write_data(archive, &byte, 1) != 1)
        goto done_entry;
    archive_entry_clear(entry);
    archive_entry_set_pathname(entry, "pkg-1.0/links/file.c");
    archive_entry_set_filetype(entry, AE_IFLNK);
    archive_entry_set_perm(entry, 0777);
    archive_entry_set_symlink(entry, "../lib/file.c");
    if (archive_write_header(archive, entry) != ARCHIVE_OK)
        goto done_entry;
    archive_entry_clear(entry);
    archive_entry_set_pathname(entry, "pkg-1.0");
    archive_entry_set_filetype(entry, AE_IFDIR);
    archive_entry_set_perm(entry, 0700);
    archive_entry_set_mtime(entry, 500, 0);
    if (archive_write_header(archive, entry) != ARCHIVE_OK)
        goto done_entry;
    archive_entry_clear(entry);
    archive_entry_set_pathname(entry, "readonly");
    archive_entry_set_filetype(entry, AE_IFDIR);
    archive_entry_set_perm(entry, 0555);
    archive_entry_set_mtime(entry, 600, 0);
    if (archive_write_header(archive, entry) != ARCHIVE_OK)
        goto done_entry;
    archive_entry_clear(entry);
    archive_entry_set_pathname(entry, "readonly/file");
    archive_entry_set_filetype(entry, AE_IFREG);
    archive_entry_set_perm(entry, 0444);
    archive_entry_set_size(entry, 1);
    if (archive_write_header(archive, entry) != ARCHIVE_OK ||
        archive_write_data(archive, &byte, 1) != 1)
        goto done_entry;
    ok = 1;
done_entry:
    archive_entry_free(entry);
done:
    if (archive != NULL) {
        archive_write_close(archive);
        archive_write_free(archive);
    }
    return ok;
}

static int make_device_archive(const char *path) {
    struct archive *archive = archive_write_new();
    struct archive_entry *entry = NULL;
    int ok = 0;
    if (archive == NULL ||
        archive_write_set_format_pax_restricted(archive) != ARCHIVE_OK ||
        archive_write_open_filename(archive, path) != ARCHIVE_OK)
        goto done;
    entry = archive_entry_new();
    archive_entry_set_pathname(entry, "dev/console");
    archive_entry_set_filetype(entry, AE_IFCHR);
    archive_entry_set_perm(entry, 0600);
    archive_entry_set_rdevmajor(entry, 5);
    archive_entry_set_rdevminor(entry, 1);
    ok = archive_write_header(archive, entry) == ARCHIVE_OK;
    archive_entry_free(entry);
done:
    if (archive != NULL) {
        archive_write_close(archive);
        archive_write_free(archive);
    }
    return ok;
}

/* Run one extraction with stderr captured and return whether the diagnostic
 * contains every expected fragment. The extraction itself must fail. */
static int rejects_with(const char *archive, const char *destination,
                        const char *source_name, CbsLocation location,
                        const char *first, const char *second) {
    char diagnostics[2048];
    FILE *capture = tmpfile();
    int saved = dup(STDERR_FILENO);
    int extracted;
    size_t length;
    if (capture == NULL || saved < 0 ||
        dup2(fileno(capture), STDERR_FILENO) < 0)
        return 0;
    extracted = cbs_extract_archive(archive, destination, source_name,
                                    location.path, NULL, location);
    fflush(stderr);
    dup2(saved, STDERR_FILENO);
    close(saved);
    rewind(capture);
    length = fread(diagnostics, 1, sizeof(diagnostics) - 1, capture);
    diagnostics[length] = '\0';
    fclose(capture);
    return !extracted && strstr(diagnostics, "error[CPDL-E6001]") != NULL &&
           strstr(diagnostics, first) != NULL &&
           strstr(diagnostics, second) != NULL;
}

/* Exercise safe archive extraction and hostile-entry rejection. */
int main(void) {
    char root[] = "/tmp/cbs-archive-XXXXXX";
    char destination[256], unsafe[256], links[256], timestamps[256];
    char implicit[256], device[256];
    char target[256], hard[256], link[256], tree[256], early[256], late[256],
        timestamp_link[256];
    char implicit_file[256], implicit_link[256], implicit_dir[256],
        implicit_parent[256], readonly[256], readonly_file[256];
    char link_target[64];
    struct stat status;
    CbsLocation location = {"archive-test", 1, 1, 0};
    if (mkdtemp(root) == NULL)
        return 1;
    snprintf(destination, sizeof(destination), "%s/out", root);
    snprintf(unsafe, sizeof(unsafe), "%s/unsafe.tar", root);
    snprintf(links, sizeof(links), "%s/links.tar", root);
    snprintf(timestamps, sizeof(timestamps), "%s/timestamps.tar", root);
    snprintf(implicit, sizeof(implicit), "%s/implicit.tar", root);
    snprintf(device, sizeof(device), "%s/device.tar", root);
    snprintf(target, sizeof(target), "%s/out/target", root);
    snprintf(hard, sizeof(hard), "%s/out/hard", root);
    snprintf(link, sizeof(link), "%s/out/dir/link", root);
    snprintf(early, sizeof(early), "%s/out/tree/early", root);
    snprintf(late, sizeof(late), "%s/out/tree/late", root);
    snprintf(tree, sizeof(tree), "%s/out/tree", root);
    snprintf(timestamp_link, sizeof(timestamp_link), "%s/out/tree/link", root);
    snprintf(implicit_file, sizeof(implicit_file), "%s/out/pkg-1.0/lib/file.c",
             root);
    snprintf(implicit_link, sizeof(implicit_link),
             "%s/out/pkg-1.0/links/file.c", root);
    snprintf(implicit_dir, sizeof(implicit_dir), "%s/out/pkg-1.0/lib", root);
    snprintf(implicit_parent, sizeof(implicit_parent), "%s/out/pkg-1.0", root);
    snprintf(readonly, sizeof(readonly), "%s/out/readonly", root);
    snprintf(readonly_file, sizeof(readonly_file), "%s/out/readonly/file",
             root);
    mkdir(destination, 0700);
    memset(link_target, 0, sizeof(link_target));
    if (!rejects_with("/dev/null", destination, "empty", location,
                      "source `empty`: ", "archive contains no members") ||
        !make_unsafe_archive(unsafe, 0) ||
        !rejects_with(unsafe, destination, "traversal", location,
                      "member \"../escape\": rejected: ", "unsafe path") ||
        !make_unsafe_archive(unsafe, 1) ||
        !rejects_with(unsafe, destination, "symlink", location,
                      "member \"link\": rejected: ",
                      "symbolic link target `/outside`") ||
        !make_link_archive(links, 0) ||
        !cbs_extract_archive(links, destination, "links", location.path, NULL,
                             location) ||
        readlink(link, link_target, sizeof(link_target) - 1) < 0 ||
        strcmp(link_target, "../target") != 0 ||
        lstat(target, &status) != 0 || !S_ISREG(status.st_mode) ||
        lstat(hard, &status) != 0 || !S_ISREG(status.st_mode) ||
        !make_link_archive(unsafe, 1) ||
        !rejects_with(unsafe, destination, "unsafe-link", location,
                      "member \"dir/link\": rejected: ",
                      "symbolic link target `../../outside`") ||
        !make_timestamp_archive(timestamps) ||
        !cbs_extract_archive(timestamps, destination, "timestamps",
                             location.path, NULL, location) ||
        stat(early, &status) != 0 || status.st_mtime != 200 ||
        stat(late, &status) != 0 || status.st_mtime != 300 ||
        stat(tree, &status) != 0 || status.st_mtime != 100 ||
        lstat(timestamp_link, &status) != 0 || status.st_mtime != 400)
        return 1;
    /* Missing parents are created; a directory member applies its mode and
     * mtime after its contents are written, in either order. */
    if (!make_implicit_archive(implicit) ||
        !cbs_extract_archive(implicit, destination, "implicit", location.path,
                             NULL, location) ||
        stat(implicit_file, &status) != 0 || !S_ISREG(status.st_mode) ||
        lstat(implicit_link, &status) != 0 || !S_ISLNK(status.st_mode) ||
        stat(implicit_dir, &status) != 0 || !S_ISDIR(status.st_mode) ||
        (status.st_mode & 07777) != 0755 ||
        stat(implicit_parent, &status) != 0 ||
        (status.st_mode & 07777) != 0700 || status.st_mtime != 500 ||
        stat(readonly, &status) != 0 || (status.st_mode & 07777) != 0555 ||
        status.st_mtime != 600 || stat(readonly_file, &status) != 0 ||
        !S_ISREG(status.st_mode))
        return 1;
    if (!make_device_archive(device) ||
        !rejects_with(device, destination, "device", location,
                      "member \"dev/console\": rejected: ",
                      "character device"))
        return 1;
    puts("archive extraction tests: PASS (safe links, mtimes, implicit "
         "parents, deferred directory modes, hostile entries, and named "
         "diagnostics)");
    return 0;
}
