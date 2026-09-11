#define _POSIX_C_SOURCE 200809L
/* Hostile-input corpus for CIXPKG verification and extraction. */
#include "cbs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int copy_file(const char *from, const char *to) {
    FILE *in = fopen(from, "rb"), *out;
    unsigned char buffer[4096];
    size_t n;
    if (!in)
        return 0;
    out = fopen(to, "wb");
    if (!out) {
        fclose(in);
        return 0;
    }
    while ((n = fread(buffer, 1, sizeof(buffer), in)) != 0)
        if (fwrite(buffer, 1, n, out) != n) {
            fclose(in);
            fclose(out);
            return 0;
        }
    if (ferror(in) || fclose(in) != 0 || fclose(out) != 0)
        return 0;
    return 1;
}

/* Mutate valid package bytes and require safe rejection. */
int main(void) {
    char root[128], source[160], manifest[160], good[160], bad[160],
        extracted[160];
    FILE *file;
    struct stat status;
    unsigned char *bytes;
    long length;
    size_t i;
    snprintf(root, sizeof(root), "/tmp/cbs-fuzz-%ld", (long)getpid());
    snprintf(source, sizeof(source), "%s/file", root);
    snprintf(manifest, sizeof(manifest), "%s.manifest", root);
    snprintf(good, sizeof(good), "%s.cixpkg", root);
    snprintf(bad, sizeof(bad), "%s.bad", root);
    snprintf(extracted, sizeof(extracted), "%s.out", root);
    if (mkdir(root, 0700) != 0 || (file = fopen(source, "wb")) == NULL ||
        fputs("fuzz corpus", file) < 0 || fclose(file) != 0 ||
        !cbs_manifest_write(root, manifest) ||
        !cbs_cixpkg_write_tree(manifest, root, good, "fuzz-1-1-x86_64") ||
        !cbs_cixpkg_verify_tree(good, NULL, 0))
        return 1;
    file = fopen(good, "rb");
    if (!file || fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) <= 0 ||
        fseek(file, 0, SEEK_SET) != 0)
        return 1;
    bytes = malloc((size_t)length);
    if (!bytes || fread(bytes, 1, (size_t)length, file) != (size_t)length ||
        fclose(file) != 0)
        return 1;
    for (i = 0; i < (size_t)length; i += (i < 352 ? 7 : 31)) {
        if (i >= 160 && i < 352)
            continue;
        if (!copy_file(good, bad))
            return 1;
        file = fopen(bad, "r+b");
        if (!file || fseek(file, (long)i, SEEK_SET) != 0 ||
            fputc(bytes[i] ^ 0x5a, file) == EOF || fclose(file) != 0)
            return 1;
        if (cbs_cixpkg_verify_tree(bad, NULL, 0) ||
            cbs_cixpkg_extract(bad, extracted)) {
            printf("mutation unexpectedly accepted at offset %zu\n", i);
            return 1;
        }
        if (i == 351)
            i = 351;
    }
    (void)status;
    free(bytes);
    puts("hostile CIXPKG corpus tests: PASS (mutated readers reject safely)");
    return 0;
}
