#define _POSIX_C_SOURCE 200809L

/* Regression tests for ELF DT_NEEDED dependency observation. */
#include "cbs.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static int saw_libc;
static int collect(const char *name, void *user) {
    (void)user;
    if (strstr(name, "libc.so") != NULL)
        saw_libc = 1;
    return 1;
}
/* Verify ELF dependency discovery and invalid-input rejection. */
int main(void) {
    char junk_path[] = "/tmp/cbs-observe-junk-XXXXXX";
    const char junk[] = "not an ELF\n";
    int junk_fd = mkstemp(junk_path);
    int elf_ok;
    int junk_rejected;

    if (junk_fd < 0)
        return 1;
    if (write(junk_fd, junk, sizeof(junk) - 1) != (ssize_t)(sizeof(junk) - 1) ||
        close(junk_fd) != 0) {
        unlink(junk_path);
        return 1;
    }

    elf_ok = cbs_observe_dependencies(collect, "./cbs", NULL) && saw_libc;
    junk_rejected = !cbs_observe_dependencies(collect, junk_path, NULL);
    unlink(junk_path);

    if (!elf_ok || !junk_rejected)
        return 1;
    puts(
        "ELF dependency observation tests: PASS (ELF and non-ELF input)");
    return 0;
}
