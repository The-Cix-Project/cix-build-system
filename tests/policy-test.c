#define _POSIX_C_SOURCE 200809L
/* Regression test for embedder finalization before manifest generation. */
#include "cbs.h"
#include "temp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int finalized(const char *root, void *user) {
    char path[4096];
    FILE *file;
    (void)user;
    if (snprintf(path, sizeof(path), "%s/policy-ran", root) >=
        (int)sizeof(path))
        return 0;
    file = fopen(path, "wb");
    if (file == NULL)
        return 0;
    return fclose(file) == 0;
}

/* Verify finalization runs before manifest generation and is recorded. */
static int run_test(const char *workspace) {
    char artifact[4160];
    char extracted[4160];
    FILE *file;
    int flag;

    if (
        snprintf(artifact, sizeof(artifact), "%s/artifact.cixpkg", workspace) >=
            (int)sizeof(artifact) ||
        snprintf(extracted, sizeof(extracted), "%s/extracted", workspace) >=
            (int)sizeof(extracted) ||
        !cbs_build_standalone_with_cache_policy(
            "tests/fixtures/standalone-smoke.cbs", workspace, artifact,
            "x86_64", NULL, NULL, finalized, NULL))
        return 1;
    file = fopen(artifact, "rb");
    if (file == NULL || fseek(file, 224, SEEK_SET) != 0 ||
        (flag = fgetc(file)) != (int)CBS_CIXPKG_FLAG_FINALIZED ||
        fclose(file) != 0)
        return 1;
    if (!cbs_cixpkg_extract(artifact, extracted))
        return 1;
    {
        char marker[4096];
        if (snprintf(marker, sizeof(marker), "%s/policy-ran", extracted) >=
                (int)sizeof(marker) ||
            access(marker, F_OK) != 0)
            return 1;
    }
    return 0;
}

/* Run the finalization checks under a private workspace and remove it. */
int main(void) {
    char workspace[4096];
    int result;
    if (!test_temp_root(workspace, sizeof(workspace), "cbs-policy"))
        return 1;
    result = run_test(workspace);
    test_remove_tree(workspace);
    if (result != 0)
        return result;
    puts("policy tests: PASS (embedder finalizer runs before manifest and is "
         "recorded)");
    return 0;
}
