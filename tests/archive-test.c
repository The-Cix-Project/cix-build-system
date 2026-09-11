#define _POSIX_C_SOURCE 200809L
/* Regression tests for archive format, traversal, and link rejection. */
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

/* Exercise safe archive extraction and hostile-entry rejection. */
int main(void) {
    char root[] = "/tmp/cbs-archive-XXXXXX";
    char destination[256], unsafe[256], links[256], target[256], hard[256], link[256];
    char link_target[64];
    struct stat status;
    CbsLocation location = {"archive-test", 1, 1, 0};
    if (mkdtemp(root) == NULL)
        return 1;
    snprintf(destination, sizeof(destination), "%s/out", root);
    snprintf(unsafe, sizeof(unsafe), "%s/unsafe.tar", root);
    snprintf(links, sizeof(links), "%s/links.tar", root);
    snprintf(target, sizeof(target), "%s/out/target", root);
    snprintf(hard, sizeof(hard), "%s/out/hard", root);
    snprintf(link, sizeof(link), "%s/out/dir/link", root);
    mkdir(destination, 0700);
    memset(link_target, 0, sizeof(link_target));
    if (cbs_extract_archive("/dev/null", destination, "empty", location.path,
                            NULL, location) ||
        !make_unsafe_archive(unsafe, 0) ||
        cbs_extract_archive(unsafe, destination, "traversal", location.path,
                            NULL, location) ||
        !make_unsafe_archive(unsafe, 1) ||
        cbs_extract_archive(unsafe, destination, "symlink", location.path, NULL,
                            location) ||
        !make_link_archive(links, 0) ||
        !cbs_extract_archive(links, destination, "links", location.path, NULL,
                             location) ||
        readlink(link, link_target, sizeof(link_target) - 1) < 0 ||
        strcmp(link_target, "../target") != 0 ||
        lstat(target, &status) != 0 || !S_ISREG(status.st_mode) ||
        lstat(hard, &status) != 0 || !S_ISREG(status.st_mode) ||
        !make_link_archive(unsafe, 1) ||
        cbs_extract_archive(unsafe, destination, "unsafe-link", location.path,
                            NULL, location))
        return 1;
    puts("archive extraction tests: PASS (safe links and hostile entries)");
    return 0;
}
