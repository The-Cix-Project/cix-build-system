/* Regression tests for dependency roles and phase selection. */
#include "cbs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_all(const char *path, size_t *length) {
    FILE *f = fopen(path, "rb");
    long n;
    char *p;
    if (!f || fseek(f, 0, SEEK_END) || (n = ftell(f)) < 0 ||
        fseek(f, 0, SEEK_SET))
        return NULL;
    p = cbs_allocate((size_t)n + 1);
    if (fread(p, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(p);
        return NULL;
    }
    fclose(f);
    p[n] = '\0';
    *length = (size_t)n;
    return p;
}
/* Verify dependency roles are selected correctly for each phase. */
int main(int argc, char **argv) {
    char *source;
    size_t length;
    CbsTokenList tokens;
    CbsNode *document;
    CbsDependencySet build, check, install;
    int ok = 1;
    memset(&tokens, 0, sizeof(tokens));
    if (argc != 2)
        return 2;
    source = read_all(argv[1], &length);
    if (!source || !cbs_lex(argv[1], source, length, &tokens))
        return 1;
    document = cbs_parse(argv[1], source, length, &tokens);
    if (!document || !cbs_validate(document, argv[1], source))
        ok = 0;
    if (ok && (!cbs_dependencies_for_phase(document, "build", &build) ||
               !cbs_dependencies_for_phase(document, "check", &check) ||
               !cbs_dependencies_for_phase(document, "install", &install) ||
               build.count != 3 || check.count != 4 || install.count != 1 ||
               !cbs_dependency_set_contains(&build, "compiler", "tcc") ||
               !cbs_dependency_set_contains(&build, "tool", "make") ||
               cbs_dependency_set_contains(&build, "tool", "tester") ||
               !cbs_dependency_set_contains(&check, "tool", "tester") ||
               !cbs_dependency_set_contains(&install, "library", "libexample")))
        ok = 0;
    if (ok)
        puts("dependency tests: PASS (role-aware phase inputs)");
    else
        fputs("dependency tests: FAIL\n", stderr);
    if (ok) {
        cbs_dependency_set_destroy(&build);
        cbs_dependency_set_destroy(&check);
        cbs_dependency_set_destroy(&install);
    }
    cbs_node_destroy(document);
    cbs_token_list_destroy(&tokens);
    free(source);
    return ok ? 0 : 1;
}
