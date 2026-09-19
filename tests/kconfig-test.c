#define _POSIX_C_SOURCE 200809L
#include "cbs.h"
#include "temp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int run_test(const char *root) {
    char base[4160], fragment[4160], output[4160], error[256], line[256];
    FILE *file;
    snprintf(base, sizeof(base), "%s/base", root);
    snprintf(fragment, sizeof(fragment), "%s/fragment", root);
    snprintf(output, sizeof(output), "%s/output", root);
    file = fopen(base, "w");
    if (file == NULL ||
        fputs("CONFIG_ALPHA=y\nCONFIG_MODULE=m\n# CONFIG_OLD is not set\n",
              file) < 0 || fclose(file) != 0)
        return 1;
    file = fopen(fragment, "w");
    if (file == NULL || fputs("CONFIG_MODULE=m\nCONFIG_NEW=n\n", file) < 0 ||
        fclose(file) != 0)
        return 1;
    if (!cbs_kconfig_merge(base, fragment, output, error, sizeof(error)))
        return 1;
    file = fopen(output, "r");
    if (file == NULL || fgets(line, sizeof(line), file) == NULL ||
        strcmp(line, "CONFIG_ALPHA=y\n") != 0 ||
        fgets(line, sizeof(line), file) == NULL ||
        strcmp(line, "CONFIG_MODULE=m\n") != 0 ||
        fgets(line, sizeof(line), file) == NULL ||
        strcmp(line, "CONFIG_OLD=n\n") != 0 ||
        fgets(line, sizeof(line), file) == NULL ||
        strcmp(line, "CONFIG_NEW=n\n") != 0)
        return 1;
    fclose(file);
    return 0;
}

/* Run the merge checks under a private root and remove it either way. */
int main(void) {
    char root[4096];
    int result;
    if (!test_temp_root(root, sizeof(root), "cbs-kconfig"))
        return 1;
    result = run_test(root);
    test_remove_tree(root);
    if (result != 0)
        return result;
    puts("kconfig merge tests: PASS (curated y/m/n states and deterministic "
         "order)");
    return 0;
}
