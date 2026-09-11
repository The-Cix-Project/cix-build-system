/* Regression tests for ELF DT_NEEDED dependency observation. */
#include "cbs.h"
#include <stdio.h>
#include <string.h>
static int saw_libc;
static int collect(const char *name, void *user) {
    (void)user;
    if (strstr(name, "libc.so") != NULL)
        saw_libc = 1;
    return 1;
}
/* Verify ELF dependency discovery and invalid-input rejection. */
int main(void) {
    if (!cbs_observe_dependencies(collect, "/bin/ls", NULL) || !saw_libc ||
        cbs_observe_dependencies(collect, "/etc/hosts", NULL))
        return 1;
    puts(
        "ELF dependency observation tests: PASS (DT_NEEDED and invalid input)");
    return 0;
}
