#ifndef CBS_TESTS_TEMP_H
#define CBS_TESTS_TEMP_H

/* Temporary-directory helpers shared by the test programs.
 *
 * Every test writes under its own root instead of fixed `/tmp` paths, honours
 * TMPDIR so the suite runs on an image without `/tmp`, and removes that root
 * before it exits. A suite run that leaves roots behind fills a small tmpfs
 * and makes later tests fail for reasons that have nothing to do with what
 * they assert. */

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* The directory the tests may write in: TMPDIR when it names an absolute
 * path, otherwise /tmp. */
static inline const char *test_temp_dir(void) {
    const char *directory = getenv("TMPDIR");
    return directory != NULL && directory[0] == '/' ? directory : "/tmp";
}

/* Create a private root named after LABEL. Returns 0 and reports why on
 * failure, so an unusable temporary directory is never mistaken for a failed
 * assertion. */
static inline int test_temp_root(char *out, size_t size, const char *label) {
    if (snprintf(out, size, "%s/%s-XXXXXX", test_temp_dir(), label) >=
        (int)size) {
        fprintf(stderr, "%s: TMPDIR path is too long: %s\n", label,
                test_temp_dir());
        return 0;
    }
    if (mkdtemp(out) == NULL) {
        fprintf(stderr, "%s: cannot create a temporary directory under %s\n",
                label, test_temp_dir());
        return 0;
    }
    return 1;
}

/* Remove a test root and everything in it. Directory modes are restored
 * first because a test may have created a deliberately read-only directory,
 * whose entries cannot be unlinked until it is writable again. */
static inline void test_remove_tree(const char *path) {
    DIR *directory;
    struct dirent *entry;
    char child[4096];
    struct stat status;

    if (path == NULL || path[0] == '\0')
        return;
    chmod(path, 0700);
    directory = opendir(path);
    if (directory != NULL) {
        while ((entry = readdir(directory)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 ||
                strcmp(entry->d_name, "..") == 0)
                continue;
            if (snprintf(child, sizeof(child), "%s/%s", path, entry->d_name) >=
                (int)sizeof(child))
                continue;
            if (lstat(child, &status) == 0 && S_ISDIR(status.st_mode))
                test_remove_tree(child);
            else
                unlink(child);
        }
        closedir(directory);
    }
    rmdir(path);
}

/* A redirected stderr, captured into a file inside the test's own root.
 * tmpfile() is not used: it opens in /tmp whatever TMPDIR says, so it returns
 * NULL on an image without /tmp or with a full one. */
typedef struct {
    FILE *file;
    char path[4096];
    int saved;
} TestCapture;

/* Begin capturing stderr. Returns 0 when the capture cannot be set up, which
 * a caller must report as a harness failure, not an assertion failure. */
static inline int test_capture_begin(TestCapture *capture, const char *root) {
    capture->file = NULL;
    capture->saved = -1;
    if (snprintf(capture->path, sizeof(capture->path), "%s/stderr-capture",
                 root) >= (int)sizeof(capture->path))
        return 0;
    capture->file = fopen(capture->path, "w+b");
    if (capture->file == NULL)
        return 0;
    capture->saved = dup(STDERR_FILENO);
    if (capture->saved < 0 ||
        dup2(fileno(capture->file), STDERR_FILENO) < 0) {
        if (capture->saved >= 0)
            close(capture->saved);
        fclose(capture->file);
        unlink(capture->path);
        capture->file = NULL;
        capture->saved = -1;
        return 0;
    }
    return 1;
}

/* Restore stderr and copy what was captured into OUT. */
static inline void test_capture_end(TestCapture *capture, char *out,
                                    size_t size) {
    size_t length = 0;

    if (out != NULL && size != 0)
        out[0] = '\0';
    if (capture->file == NULL)
        return;
    fflush(stderr);
    if (capture->saved >= 0) {
        dup2(capture->saved, STDERR_FILENO);
        close(capture->saved);
        capture->saved = -1;
    }
    rewind(capture->file);
    if (out != NULL && size != 0) {
        length = fread(out, 1, size - 1, capture->file);
        out[length] = '\0';
    }
    fclose(capture->file);
    capture->file = NULL;
    unlink(capture->path);
}

#endif
