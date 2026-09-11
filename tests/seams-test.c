#include "cbs.h"
#include <stdio.h>
#include <string.h>

static int entered, left, checked, observed;
static int enter(const char *root, void *user) { entered = strcmp(root, "/tmp") == 0; (void)user; return entered; }
static int leave(const char *root, void *user) { left = strcmp(root, "/tmp") == 0; (void)user; return left; }
static int verify(const unsigned char *data, size_t length, void *user) { checked = length == 4 && memcmp(data, "cbs!", 4) == 0; (void)user; return checked; }
static int request(const char *op, const char *payload, char *response, size_t size, void *user) { (void)user; if (strcmp(op, "ping") != 0 || strcmp(payload, "x") != 0) return 0; snprintf(response, size, "pong"); return 1; }
static int health(void *user) { (void)user; return 1; }
static int observe(const char *name, void *user) { (void)user; if (strstr(name, "libc.so") != NULL) observed = 1; return 1; }

int main(void)
{
    FILE *file = fopen("/tmp/cbs-signature-seam", "wb");
    char response[16];
    if (!file || fputs("cbs!", file) < 0 || fclose(file) != 0 ||
        !cbs_sandbox_run(enter, leave, "/tmp", NULL) || !entered || !left ||
        !cbs_verify_signature(verify, "/tmp/cbs-signature-seam", NULL) || !checked ||
        !cbs_daemon_request(request, NULL, "ping", "x", response, sizeof(response)) ||
        strcmp(response, "pong") != 0 || !cbs_service_health(health, NULL) ||
        !cbs_observe_dependencies(observe, "/bin/ls", NULL) || !observed) return 1;
    puts("embedding seam tests: PASS (sandbox, signature, daemon, health, ELF observer)");
    return 0;
}
