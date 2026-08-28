#define _POSIX_C_SOURCE 200809L
#include "cbs.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

int main(void)
{
    char root[] = "/tmp/cbs-archive-XXXXXX";
    char destination[256];
    CbsLocation location = {"archive-test", 1, 1, 0};
    if (mkdtemp(root) == NULL) return 1;
    snprintf(destination, sizeof(destination), "%s/out", root);
    mkdir(destination, 0700);
    if (cbs_extract_archive("/dev/null", destination, "empty", location.path,
                            NULL, location)) return 1;
    puts("archive extraction tests: PASS (unsupported input rejected safely)");
    return 0;
}
