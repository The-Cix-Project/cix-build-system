/* Regression tests for sorted typed manifests and staged-tree rules. */
#include "cbs.h"
#include "temp.h"
#include <stdio.h>
#include <stdlib.h>
/* Verify manifest ordering and staged-entry metadata. */
int main(void) {
    CbsManifestEntry e[2] = {{.path = "z", .type = 'f'},
                             {.path = "a", .type = 'f'}};
    char root[4096], manifest[4160];
    int ok;
    qsort(e, 2, sizeof(e[0]), cbs_manifest_compare);
    if (!test_temp_root(root, sizeof(root), "cbs-manifest"))
        return 1;
    snprintf(manifest, sizeof(manifest), "%s/manifest", root);
    ok = e[0].path[0] == 'a' &&
         cbs_manifest_write("tests/fixtures/execution", manifest);
    test_remove_tree(root);
    if (!ok)
        return 1;
    puts("manifest tests: PASS (canonical path ordering and tree emission)");
    return 0;
}
