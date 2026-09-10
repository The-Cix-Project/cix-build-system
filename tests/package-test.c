#define _POSIX_C_SOURCE 200809L
#include "cbs.h"
#include <stdio.h>
#include <unistd.h>

static int flip_byte(const char *path, long offset)
{
    FILE *file = fopen(path, "r+b");
    int value;
    if (file == NULL || fseek(file, offset, SEEK_SET) != 0 ||
        (value = fgetc(file)) == EOF || fseek(file, offset, SEEK_SET) != 0 ||
        fputc(value ^ 0xff, file) == EOF || fclose(file) != 0) {
        if (file != NULL) fclose(file);
        return 0;
    }
    return 1;
}

static int truncate_file(const char *path, off_t length)
{
    FILE *file = fopen(path, "r+b");
    int descriptor;
    int result;
    if (file == NULL) return 0;
    descriptor = fileno(file);
    result = descriptor >= 0 && ftruncate(descriptor, length) == 0;
    if (fclose(file) != 0) result = 0;
    return result;
}

int main(void)
{
    char in[] = "/tmp/cixpkg-in", out[] = "/tmp/cixpkg-out";
    char round[] = "/tmp/cixpkg-round";
    char identity[32];
    FILE *file = fopen(in, "wb");
    if (file == NULL) return 1;
    fputs("payload", file);
    fclose(file);
    if (!cbs_cixpkg_compress(in, out) || !cbs_cixpkg_decompress(out, round) ||
        !cbs_build_package("cbs.cbs", "tests/fixtures/execution",
                           "/tmp/cixpkg-build") ||
        !cbs_cixpkg_verify_tree("/tmp/cixpkg-build", identity,
                                sizeof(identity)) ||
        !cbs_cixpkg_extract("/tmp/cixpkg-build", "/tmp/cixpkg-extracted") ||
        access("/tmp/cixpkg-extracted/argv.cbs", F_OK) != 0) return 1;
    if (!flip_byte("/tmp/cixpkg-build", 32) ||
        cbs_cixpkg_verify_tree("/tmp/cixpkg-build", NULL, 0)) return 1;
    if (!flip_byte("/tmp/cixpkg-build", 32) ||
        !flip_byte("/tmp/cixpkg-build", 400) ||
        cbs_cixpkg_verify_tree("/tmp/cixpkg-build", NULL, 0)) return 1;
    if (!truncate_file("/tmp/cixpkg-build", 351) ||
        cbs_cixpkg_verify_tree("/tmp/cixpkg-build", NULL, 0)) return 1;
    puts("CIXPKG tests: PASS (writer, reader, pipeline, corruption gates)");
    return 0;
}
