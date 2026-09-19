#define _POSIX_C_SOURCE 200809L
/* Regression tests for CIXPKG creation, verification, and corruption gates. */
#include "cbs.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int flip_byte(const char *path, long offset) {
    FILE *file = fopen(path, "r+b");
    int value;
    if (file == NULL || fseek(file, offset, SEEK_SET) != 0 ||
        (value = fgetc(file)) == EOF || fseek(file, offset, SEEK_SET) != 0 ||
        fputc(value ^ 0xff, file) == EOF || fclose(file) != 0) {
        if (file != NULL)
            fclose(file);
        return 0;
    }
    return 1;
}

static int truncate_file(const char *path, off_t length) {
    FILE *file = fopen(path, "r+b");
    int descriptor;
    int result;
    if (file == NULL)
        return 0;
    descriptor = fileno(file);
    result = descriptor >= 0 && ftruncate(descriptor, length) == 0;
    if (fclose(file) != 0)
        result = 0;
    return result;
}

/* Verify package creation, round trips, and corruption gates. */
int main(void) {
    char in[] = "/tmp/cixpkg-in", out[] = "/tmp/cixpkg-out";
    char round[] = "/tmp/cixpkg-round";
    char extracted[64], extracted_file[128];
    char identity[32];
    FILE *file = fopen(in, "wb");
    if (file == NULL)
        return 1;
    fputs("payload", file);
    fclose(file);
    snprintf(extracted, sizeof(extracted), "/tmp/cixpkg-extracted-%ld",
             (long)getpid());
    if (!cbs_cixpkg_compress(in, out) || !cbs_cixpkg_decompress(out, round) ||
        !cbs_build_package("cbs.cbs", "tests/fixtures/execution",
                           "/tmp/cixpkg-build") ||
        !cbs_cixpkg_verify_tree("/tmp/cixpkg-build", identity,
                                sizeof(identity)) ||
        !cbs_cixpkg_extract("/tmp/cixpkg-build", extracted) ||
        snprintf(extracted_file, sizeof(extracted_file), "%s/argv.cbs",
                 extracted) >= (int)sizeof(extracted_file) ||
        access(extracted_file, F_OK) != 0)
        return 1;
    if (!flip_byte("/tmp/cixpkg-build", 32) ||
        cbs_cixpkg_verify_tree("/tmp/cixpkg-build", NULL, 0))
        return 1;
    if (!flip_byte("/tmp/cixpkg-build", 32) ||
        !flip_byte("/tmp/cixpkg-build", 400) ||
        cbs_cixpkg_verify_tree("/tmp/cixpkg-build", NULL, 0))
        return 1;
    if (!truncate_file("/tmp/cixpkg-build", 351) ||
        cbs_cixpkg_verify_tree("/tmp/cixpkg-build", NULL, 0))
        return 1;

    /* Identity occupies bytes 160-223 and must never reach the flags byte at
     * 224: a 64-byte identity round trips, and a longer one is refused at
     * write time rather than silently truncated or written over the flags. */
    {
        char manifest[] = "/tmp/cixpkg-identity-manifest";
        char package[] = "/tmp/cixpkg-identity";
        char longest[65];
        char excessive[66];
        char read_back[129];
        memset(longest, 'i', sizeof(longest) - 1);
        longest[sizeof(longest) - 1] = '\0';
        memset(excessive, 'i', sizeof(excessive) - 1);
        excessive[sizeof(excessive) - 1] = '\0';
        if (!cbs_manifest_write("tests/fixtures/execution", manifest))
            return 1;
        if (!cbs_cixpkg_write_tree(manifest, "tests/fixtures/execution",
                                   package, longest) ||
            !cbs_cixpkg_verify_tree(package, read_back, sizeof(read_back)) ||
            strcmp(read_back, longest) != 0)
            return 1;
        if (cbs_cixpkg_write_tree(manifest, "tests/fixtures/execution",
                                  package, excessive))
            return 1;
    }
    puts("CIXPKG tests: PASS (writer, reader, pipeline, identity bounds, and "
         "corruption gates)");
    return 0;
}
