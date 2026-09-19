#define _POSIX_C_SOURCE 200809L
/* Regression tests for archive format, traversal, link rejection, implicit
 * parent directories, deferred directory metadata, and named diagnostics. */
#include "cbs.h"
#include "temp.h"
#include <archive.h>
#include <archive_entry.h>
#include <dirent.h>
#include <errno.h>
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

/* Every failure says which check failed and where, because a silent exit
 * code cannot be diagnosed from a build log. */
static int failures;

static void report(int line, const char *what, const char *detail) {
    fprintf(stderr, "archive-test: FAILED at line %d: %s%s%s\n", line, what,
            detail == NULL ? "" : ": ", detail == NULL ? "" : detail);
    ++failures;
}

#define CHECK(condition, what)                                               \
    do {                                                                     \
        if (!(condition)) {                                                  \
            report(__LINE__, (what), NULL);                                  \
            goto cleanup;                                                    \
        }                                                                    \
    } while (0)

#define CHECK_ERRNO(condition, what)                                         \
    do {                                                                     \
        if (!(condition)) {                                                  \
            report(__LINE__, (what), strerror(errno));                       \
            goto cleanup;                                                    \
        }                                                                    \
    } while (0)

/* Outcome of one rejection check, so a harness failure is never reported as
 * a failed assertion. */
typedef enum {
    REJECTED_AS_EXPECTED,
    REJECTION_MISMATCH,
    CAPTURE_UNAVAILABLE
} RejectionResult;

/* Run one extraction with stderr captured to a file inside the test's own
 * root. tmpfile() is deliberately not used: it opens in /tmp whatever TMPDIR
 * says, so it fails on an image without /tmp or with a full one, and that
 * failure is not an assertion failure. */
static RejectionResult rejects_with(const char *archive,
                                    const char *destination,
                                    const char *source_name,
                                    CbsLocation location, const char *root,
                                    const char *first, const char *second,
                                    char *diagnostics, size_t size) {
    char capture_path[4096];
    FILE *capture;
    int saved;
    int extracted;
    size_t length;

    if (snprintf(capture_path, sizeof(capture_path), "%s/diagnostics", root) >=
        (int)sizeof(capture_path))
        return CAPTURE_UNAVAILABLE;
    capture = fopen(capture_path, "w+b");
    if (capture == NULL)
        return CAPTURE_UNAVAILABLE;
    saved = dup(STDERR_FILENO);
    if (saved < 0 || dup2(fileno(capture), STDERR_FILENO) < 0) {
        if (saved >= 0)
            close(saved);
        fclose(capture);
        unlink(capture_path);
        return CAPTURE_UNAVAILABLE;
    }
    extracted = cbs_extract_archive(archive, destination, source_name,
                                    location.path, NULL, location);
    fflush(stderr);
    if (dup2(saved, STDERR_FILENO) < 0) {
        close(saved);
        fclose(capture);
        unlink(capture_path);
        return CAPTURE_UNAVAILABLE;
    }
    close(saved);
    rewind(capture);
    length = fread(diagnostics, 1, size - 1, capture);
    diagnostics[length] = '\0';
    fclose(capture);
    unlink(capture_path);
    if (extracted)
        return REJECTION_MISMATCH;
    return strstr(diagnostics, "error[CPDL-E6001]") != NULL &&
                   strstr(diagnostics, first) != NULL &&
                   strstr(diagnostics, second) != NULL
               ? REJECTED_AS_EXPECTED
               : REJECTION_MISMATCH;
}

/* Assert one rejection, naming the fragment that was missing when it fails. */
#define CHECK_REJECTED(archive, name, first, second)                         \
    do {                                                                     \
        RejectionResult outcome =                                            \
            rejects_with((archive), destination, (name), location, root,     \
                         (first), (second), diagnostics, sizeof(diagnostics)); \
        if (outcome == CAPTURE_UNAVAILABLE) {                                \
            report(__LINE__, "cannot capture stderr for `" name "`",         \
                   strerror(errno));                                         \
            goto cleanup;                                                    \
        }                                                                    \
        if (outcome != REJECTED_AS_EXPECTED) {                               \
            fprintf(stderr,                                                  \
                    "archive-test: expected `%s` and `%s`; diagnostic was: "  \
                    "%s\n",                                                  \
                    (first), (second),                                       \
                    diagnostics[0] == '\0' ? "(extraction succeeded)"        \
                                           : diagnostics);                   \
            report(__LINE__, "`" name "` was not rejected as specified",     \
                   NULL);                                                    \
            goto cleanup;                                                    \
        }                                                                    \
    } while (0)

/* Exercise safe archive extraction and hostile-entry rejection. */
int main(void) {
    char root[4096];
    char destination[4160], unsafe[4160], links[4160], timestamps[4160];
    char implicit[4160], device[4160];
    char target[4160], hard[4160], link[4160], tree[4160], early[4160],
        late[4160], timestamp_link[4160];
    char implicit_file[4160], implicit_link[4160], implicit_dir[4160],
        implicit_parent[4160], readonly[4160], readonly_file[4160];
    char link_target[64];
    char diagnostics[2048];
    struct stat status;
    CbsLocation location = {"archive-test", 1, 1, 0};
    ssize_t length;

    if (!test_temp_root(root, sizeof(root), "cbs-archive"))
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
    snprintf(implicit_link, sizeof(implicit_link), "%s/out/pkg-1.0/links/file.c",
             root);
    snprintf(implicit_dir, sizeof(implicit_dir), "%s/out/pkg-1.0/lib", root);
    snprintf(implicit_parent, sizeof(implicit_parent), "%s/out/pkg-1.0", root);
    snprintf(readonly, sizeof(readonly), "%s/out/readonly", root);
    snprintf(readonly_file, sizeof(readonly_file), "%s/out/readonly/file", root);
    memset(link_target, 0, sizeof(link_target));
    CHECK_ERRNO(mkdir(destination, 0700) == 0, "cannot create the output root");

    CHECK_REJECTED("/dev/null", "empty", "source `empty`: ",
                   "archive contains no members");
    CHECK(make_unsafe_archive(unsafe, 0), "cannot write the traversal archive");
    CHECK_REJECTED(unsafe, "traversal", "member \"../escape\": rejected: ",
                   "unsafe path");
    CHECK(make_unsafe_archive(unsafe, 1), "cannot write the symlink archive");
    CHECK_REJECTED(unsafe, "symlink", "member \"link\": rejected: ",
                   "symbolic link target `/outside`");

    CHECK(make_link_archive(links, 0), "cannot write the link archive");
    CHECK(cbs_extract_archive(links, destination, "links", location.path, NULL,
                              location),
          "a safe link archive was rejected");
    length = readlink(link, link_target, sizeof(link_target) - 1);
    CHECK_ERRNO(length >= 0, "the extracted symbolic link is missing");
    link_target[length] = '\0';
    CHECK(strcmp(link_target, "../target") == 0,
          "the symbolic link target was not preserved");
    CHECK_ERRNO(lstat(target, &status) == 0, "the link target file is missing");
    CHECK(S_ISREG(status.st_mode), "the link target is not a regular file");
    CHECK_ERRNO(lstat(hard, &status) == 0, "the hard link is missing");
    CHECK(S_ISREG(status.st_mode), "the hard link is not a regular file");

    CHECK(make_link_archive(unsafe, 1),
          "cannot write the climbing-link archive");
    CHECK_REJECTED(unsafe, "unsafe-link", "member \"dir/link\": rejected: ",
                   "symbolic link target `../../outside`");

    CHECK(make_timestamp_archive(timestamps),
          "cannot write the timestamp archive");
    CHECK(cbs_extract_archive(timestamps, destination, "timestamps",
                              location.path, NULL, location),
          "the timestamp archive was rejected");
    CHECK_ERRNO(stat(early, &status) == 0, "tree/early is missing");
    CHECK(status.st_mtime == 200, "tree/early kept the wrong mtime");
    CHECK_ERRNO(stat(late, &status) == 0, "tree/late is missing");
    CHECK(status.st_mtime == 300, "tree/late kept the wrong mtime");
    CHECK_ERRNO(stat(tree, &status) == 0, "tree/ is missing");
    CHECK(status.st_mtime == 100, "the directory kept the wrong mtime");
    CHECK_ERRNO(lstat(timestamp_link, &status) == 0, "tree/link is missing");
    CHECK(status.st_mtime == 400, "the symbolic link kept the wrong mtime");

    /* Missing parents are created; a directory member applies its mode and
     * mtime after its contents are written, in either order. */
    CHECK(make_implicit_archive(implicit),
          "cannot write the directory-less archive");
    CHECK(cbs_extract_archive(implicit, destination, "implicit", location.path,
                              NULL, location),
          "the directory-less archive was rejected");
    CHECK_ERRNO(stat(implicit_file, &status) == 0,
                "the file under implicit parents is missing");
    CHECK(S_ISREG(status.st_mode), "the implicit-parent entry is not a file");
    CHECK_ERRNO(lstat(implicit_link, &status) == 0,
                "the link in a symlink-only directory is missing");
    CHECK(S_ISLNK(status.st_mode), "the symlink-only entry is not a link");
    CHECK_ERRNO(stat(implicit_dir, &status) == 0,
                "the implicitly created parent is missing");
    CHECK(S_ISDIR(status.st_mode) && (status.st_mode & 07777) == 0755,
          "an implicitly created parent has the wrong mode");
    CHECK_ERRNO(stat(implicit_parent, &status) == 0,
                "the late directory member is missing");
    CHECK((status.st_mode & 07777) == 0700,
          "a directory member listed after its contents lost its mode");
    CHECK(status.st_mtime == 500,
          "a directory member listed after its contents lost its mtime");
    CHECK_ERRNO(stat(readonly, &status) == 0,
                "the read-only directory member is missing");
    CHECK((status.st_mode & 07777) == 0555,
          "the read-only directory member lost its mode");
    CHECK(status.st_mtime == 600,
          "the read-only directory member lost its mtime");
    CHECK_ERRNO(stat(readonly_file, &status) == 0,
                "a file under a read-only directory member is missing");
    CHECK(S_ISREG(status.st_mode),
          "the entry under a read-only directory is not a file");

    CHECK(make_device_archive(device), "cannot write the device archive");
    CHECK_REJECTED(device, "device", "member \"dev/console\": rejected: ",
                   "character device");

cleanup:
    test_remove_tree(root);
    if (failures != 0) {
        fprintf(stderr, "archive-test: %d check(s) failed under %s\n", failures,
                root);
        return 1;
    }
    puts("archive extraction tests: PASS (safe links, mtimes, implicit "
         "parents, deferred directory modes, hostile entries, and named "
         "diagnostics)");
    return 0;
}
