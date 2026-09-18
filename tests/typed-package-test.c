#define _POSIX_C_SOURCE 200809L
/* Regression tests for typed CIXPKG entries and metadata safety. */
#include "cbs.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Verify typed entries, modes, links, empty directories, the exact typed-tree
 * round trip, and setuid rejection.
 */
int main(void) {
    char root[128];
    char manifest[256], package[256], extracted[256], path[256], target[64];
    char round_manifest[256], round_package[256];
    FILE *file;
    struct stat status;
    ssize_t length;

    snprintf(root, sizeof(root), "/tmp/cbs-typed-%ld", (long)getpid());
    if (mkdir(root, 0700) != 0)
        return 1;
    snprintf(manifest, sizeof(manifest), "%s.manifest", root);
    snprintf(package, sizeof(package), "%s.cixpkg", root);
    snprintf(extracted, sizeof(extracted), "%s.out", root);
    snprintf(round_manifest, sizeof(round_manifest), "%s.out.manifest", root);
    snprintf(round_package, sizeof(round_package), "%s.out.cixpkg", root);
    snprintf(path, sizeof(path), "%s/bin", root);
    if (mkdir(path, 0755) != 0)
        return 1;
    snprintf(path, sizeof(path), "%s/empty", root);
    if (mkdir(path, 0755) != 0)
        return 1;
    snprintf(path, sizeof(path), "%s/bin/tool", root);
    file = fopen(path, "wb");
    if (file == NULL || fputs("tool", file) < 0 || fclose(file) != 0 ||
        chmod(path, 0755) != 0)
        return 1;
    snprintf(path, sizeof(path), "%s/bin/link", root);
    if (symlink("/usr/bin/tool", path) != 0)
        return 1;
    snprintf(path, sizeof(path), "%s/bin/relative", root);
    if (symlink("../empty", path) != 0 || !cbs_manifest_write(root, manifest) ||
        !cbs_cixpkg_write_tree(manifest, root, package, "typed-1-1-x86_64") ||
        !cbs_cixpkg_verify_tree(package, NULL, 0) ||
        !cbs_cixpkg_extract(package, extracted))
        return 1;
    snprintf(path, sizeof(path), "%s/empty", extracted);
    if (stat(path, &status) != 0 || !S_ISDIR(status.st_mode))
        return 1;
    snprintf(path, sizeof(path), "%s/bin/tool", extracted);
    if (stat(path, &status) != 0 || (status.st_mode & 07777) != 0755)
        return 1;
    snprintf(path, sizeof(path), "%s/bin/link", extracted);
    length = readlink(path, target, sizeof(target) - 1);
    if (length < 0)
        return 1;
    target[length] = '\0';
    if (strcmp(target, "/usr/bin/tool") != 0)
        return 1;
    snprintf(path, sizeof(path), "%s/bin/relative", extracted);
    length = readlink(path, target, sizeof(target) - 1);
    if (length < 0)
        return 1;
    target[length] = '\0';
    if (strcmp(target, "../empty") != 0)
        return 1;
    /* The round trip is exact: re-manifesting the extracted tree reproduces
     * every path, type, mode, size, digest, and link target, and re-packaging
     * it reproduces the original artifact bytes.
     */
    if (!cbs_manifest_write(extracted, round_manifest) ||
        !cbs_compare_files(manifest, round_manifest) ||
        !cbs_cixpkg_write_tree(round_manifest, extracted, round_package,
                               "typed-1-1-x86_64") ||
        !cbs_compare_files(package, round_package))
        return 1;
    snprintf(path, sizeof(path), "%s/bin/escape", root);
    if (symlink("../../outside", path) != 0 ||
        cbs_manifest_write(root, manifest))
        return 1;
    snprintf(path, sizeof(path), "%s/bin/setuid", root);
    file = fopen(path, "wb");
    if (file == NULL || fputs("bad", file) < 0 || fclose(file) != 0 ||
        chmod(path, 04755) != 0 || cbs_manifest_write(root, manifest))
        return 1;
    puts("typed CIXPKG tests: PASS (empty directories, modes, confined links, "
         "exact round trip, and setuid rejection)");
    return 0;
}
