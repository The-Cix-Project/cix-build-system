/* Optional detached-signature verification seam for embedders. */
#include "cbs.h"
#include <stdio.h>
#include <stdlib.h>
int cbs_verify_signature(CbsSignatureVerifier verifier, const char *path,
                         void *user) {
    FILE *f;
    long n;
    unsigned char *d;
    int ok;
    if (!verifier || !path)
        return 0;
    f = fopen(path, "rb");
    if (!f || fseek(f, 0, SEEK_END) || (n = ftell(f)) < 0 ||
        fseek(f, 0, SEEK_SET)) {
        if (f)
            fclose(f);
        return 0;
    }
    d = malloc((size_t)n);
    if (!d || fread(d, 1, (size_t)n, f) != (size_t)n) {
        free(d);
        fclose(f);
        return 0;
    }
    fclose(f);
    ok = verifier(d, (size_t)n, user);
    free(d);
    return ok;
}
