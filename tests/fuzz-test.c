#define _POSIX_C_SOURCE 200809L
/* Hostile-input corpus for CIXPKG verification and extraction. */
#include "cbs.h"
#include "temp.h"
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
static int run_test(const char *root) {
    char tree[4160], source[4160], manifest[4160], good[4160], bad[4160],
        extracted[4160];
    FILE *file;
    struct stat status;
    unsigned char *bytes;
    long length;
    size_t i;
    /* The staged tree is a directory inside the root so every file this
     * test writes is removed with the root. */
    snprintf(tree, sizeof(tree), "%s/tree", root);
    snprintf(source, sizeof(source), "%s/file", tree);
    snprintf(manifest, sizeof(manifest), "%s/manifest", root);
    snprintf(good, sizeof(good), "%s/good.cixpkg", root);
    snprintf(bad, sizeof(bad), "%s/bad.cixpkg", root);
    snprintf(extracted, sizeof(extracted), "%s/extracted", root);
    if (mkdir(tree, 0700) != 0 || (file = fopen(source, "wb")) == NULL ||
        fputs("fuzz corpus", file) < 0 || fclose(file) != 0 ||
        !cbs_manifest_write(tree, manifest) ||
        !cbs_cixpkg_write_tree(manifest, tree, good, "fuzz-1-1-x86_64") ||
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
    return 0;
}

/* Run the mutation corpus under a private root and remove it either way. */
int main(void) {
    char root[4096];
    int result;
    if (!test_temp_root(root, sizeof(root), "cbs-fuzz"))
        return 1;
    result = run_test(root);
    test_remove_tree(root);
    if (result != 0)
        return result;
    puts("hostile CIXPKG corpus tests: PASS (mutated readers reject safely)");
    return 0;
}
