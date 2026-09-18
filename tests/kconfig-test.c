#define _POSIX_C_SOURCE 200809L
#include "cbs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    char root[] = "/tmp/cbs-kconfig-XXXXXX";
    char base[256], fragment[256], output[256], error[256], line[256];
    FILE *file;
    if (mkdtemp(root) == NULL)
        return 1;
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
    puts("kconfig merge tests: PASS (curated y/m/n states and deterministic order)");
    return 0;
}
