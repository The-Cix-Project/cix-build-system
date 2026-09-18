#define _POSIX_C_SOURCE 200809L
/* Regression tests for the byte-identical reproducibility gate. */
#include "cbs.h"

#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

static int write_text(const char *path, const char *text) {
    FILE *file = fopen(path, "wb");
    if (file == NULL)
        return 0;
    if (fputs(text, file) < 0) {
        fclose(file);
        return 0;
    }
    return fclose(file) == 0;
}

/* Verify the comparison gate rejects every kind of difference and that two
 * independent standalone builds of one recipe produce identical artifacts.
 */
int main(void) {
    char root[128], left[192], right[192];
    char workspace_a[192], workspace_b[192], artifact_a[192], artifact_b[192];

    snprintf(root, sizeof(root), "/tmp/cbs-repro-%ld", (long)getpid());
    if (mkdir(root, 0700) != 0)
        return 1;
    snprintf(left, sizeof(left), "%s/left", root);
    snprintf(right, sizeof(right), "%s/right", root);
    /* Equal bytes pass; a changed byte, a shorter or longer file, and a
     * missing file all fail.
     */
    if (!write_text(left, "same") || !write_text(right, "same") ||
        !cbs_compare_files(left, right))
        return 1;
    if (!write_text(right, "sane") || cbs_compare_files(left, right))
        return 1;
    if (!write_text(right, "sam") || cbs_compare_files(left, right))
        return 1;
    if (!write_text(right, "same!") || cbs_compare_files(left, right))
        return 1;
    if (unlink(right) != 0 || cbs_compare_files(left, right))
        return 1;
    /* Two builds in separate workspaces must emit byte-identical CIXPKG
     * artifacts: the workspace path must not leak into the package. Leaks of
     * build time or process identity are covered by cli-build-test.sh and
     * upstream-smoke-test.sh, which rebuild in separate processes.
     */
    snprintf(workspace_a, sizeof(workspace_a), "%s/workspace-a", root);
    snprintf(workspace_b, sizeof(workspace_b), "%s/workspace-b", root);
    snprintf(artifact_a, sizeof(artifact_a), "%s/a.cixpkg", root);
    snprintf(artifact_b, sizeof(artifact_b), "%s/b.cixpkg", root);
    if (mkdir(workspace_a, 0700) != 0 || mkdir(workspace_b, 0700) != 0)
        return 1;
    if (!cbs_build_standalone_with_cache("tests/fixtures/repro-tree.cbs",
                                         workspace_a, artifact_a, "x86_64",
                                         NULL, NULL) ||
        !cbs_build_standalone_with_cache("tests/fixtures/repro-tree.cbs",
                                         workspace_b, artifact_b, "x86_64",
                                         NULL, NULL) ||
        !cbs_cixpkg_verify_tree(artifact_a, NULL, 0) ||
        !cbs_compare_files(artifact_a, artifact_b))
        return 1;
    puts("reproducibility tests: PASS (byte mismatch gate and identical "
         "independent builds)");
    return 0;
}
