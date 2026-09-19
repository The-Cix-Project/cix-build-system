/* Regression tests for CBS embedding seams and adapter boundaries. */
#include "cbs.h"
#include "temp.h"
#include <stdio.h>
#include <string.h>

static int entered, left, checked, observed;
/* The sandbox root the hooks must be handed, set before the seam runs. */
static const char *expected_root;
static int enter(const char *root, void *user) {
    entered = strcmp(root, expected_root) == 0;
    (void)user;
    return entered;
}
static int leave(const char *root, void *user) {
    left = strcmp(root, expected_root) == 0;
    (void)user;
    return left;
}
static int verify(const unsigned char *data, size_t length, void *user) {
    checked = length == 4 && memcmp(data, "cbs!", 4) == 0;
    (void)user;
    return checked;
}
static int request(const char *op, const char *payload, char *response,
                   size_t size, void *user) {
    (void)user;
    if (strcmp(op, "ping") != 0 || strcmp(payload, "x") != 0)
        return 0;
    snprintf(response, size, "pong");
    return 1;
}
static int health(void *user) {
    (void)user;
    return 1;
}
static int observe(const char *name, void *user) {
    (void)user;
    if (strstr(name, "libc.so") != NULL)
        observed = 1;
    return 1;
}

/* Verify each public embedding seam with a deterministic callback. */
int main(void) {
    char root[4096], signature[4160];
    FILE *file;
    char response[16];
    if (!test_temp_root(root, sizeof(root), "cbs-seams"))
        return 1;
    expected_root = root;
    snprintf(signature, sizeof(signature), "%s/signature-seam", root);
    file = fopen(signature, "wb");
    if (!file || fputs("cbs!", file) < 0 || fclose(file) != 0 ||
        !cbs_sandbox_run(enter, leave, root, NULL) || !entered || !left ||
        !cbs_verify_signature(verify, signature, NULL) ||
        !checked ||
        !cbs_daemon_request(request, NULL, "ping", "x", response,
                            sizeof(response)) ||
        strcmp(response, "pong") != 0 || !cbs_service_health(health, NULL) ||
        !cbs_observe_dependencies(observe, "./cbs", NULL) || !observed) {
        test_remove_tree(root);
        return 1;
    }
    test_remove_tree(root);
    puts("embedding seam tests: PASS (sandbox, signature, daemon, health, ELF "
         "observer)");
    return 0;
}
