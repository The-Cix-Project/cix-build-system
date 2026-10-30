#define _POSIX_C_SOURCE 200809L
/* Regression tests for CIXPKG creation, verification, and corruption gates. */
#include "cbs.h"
#include "temp.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
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
static int run_test(const char *root) {
    char in[4160], out[4160], round[4160], build[4160];
    char extracted[4160], extracted_file[4256];
    char identity[32];
    FILE *file;

    snprintf(in, sizeof(in), "%s/in", root);
    snprintf(out, sizeof(out), "%s/out", root);
    snprintf(round, sizeof(round), "%s/round", root);
    snprintf(build, sizeof(build), "%s/build.cixpkg", root);
    snprintf(extracted, sizeof(extracted), "%s/extracted", root);
    file = fopen(in, "wb");
    if (file == NULL)
        return 1;
    fputs("payload", file);
    fclose(file);
    if (!cbs_cixpkg_compress(in, out) || !cbs_cixpkg_decompress(out, round) ||
        !cbs_build_package("cbs.cbs", "tests/fixtures/execution",
                           build) ||
        !cbs_cixpkg_verify_tree(build, identity,
                                sizeof(identity)) ||
        !cbs_cixpkg_extract(build, extracted) ||
        snprintf(extracted_file, sizeof(extracted_file), "%s/argv.cbs",
                 extracted) >= (int)sizeof(extracted_file) ||
        access(extracted_file, F_OK) != 0)
        return 1;

    /* Every embedder material that participates in execution must perturb the
     * key, while environment ordering remains canonical. */
    {
        char firmware[4256], finalizer[4256];
        char command_root[4256], library_root[4256], command_path[4256];
        char command[4256];
        char baseline[65], changed[65];
        CbsEnvironmentBinding environment[2] = {
            {"CBS_TEST_B", "two"}, {"CBS_TEST_A", "one"}};
        CbsFingerprintContext materials = {0};
        snprintf(command_root, sizeof(command_root), "%s/context-commands", root);
        snprintf(library_root, sizeof(library_root), "%s/context-libraries", root);
        snprintf(command_path, sizeof(command_path), "%s", command_root);
        snprintf(command, sizeof(command), "%s/true", command_root);
        if (mkdir(command_root, 0700) != 0 || mkdir(library_root, 0700) != 0)
            return 1;
        file = fopen(command, "wb");
        if (file == NULL)
            return 1;
        fputs("tool", file);
        fclose(file);
        if (chmod(command, 0755) != 0)
            return 1;
        snprintf(firmware, sizeof(firmware), "%s/firmware", root);
        snprintf(finalizer, sizeof(finalizer), "%s/finalizer", root);
        file = fopen(firmware, "wb");
        if (file == NULL)
            return 1;
        fputs("firmware", file);
        fclose(file);
        file = fopen(finalizer, "wb");
        if (file == NULL)
            return 1;
        fputs("finalizer", file);
        fclose(file);
        {
            int base_ok = cbs_build_fingerprint(
                "tests/fixtures/standalone-smoke.cbs", "x86_64", NULL, NULL,
                NULL, 0, baseline);
            int context_ok = cbs_build_fingerprint_with_context(
                "tests/fixtures/standalone-smoke.cbs", "x86_64", command_path,
                library_root, NULL, 0, &materials, changed);
            if (!base_ok || !context_ok || strcmp(baseline, changed) == 0) {
                return 1;
            }
        }
        materials.firmware_root = firmware;
        if (!cbs_build_fingerprint_with_context(
                "tests/fixtures/standalone-smoke.cbs", "x86_64", command_path,
                library_root,
                NULL, 0, &materials, changed) || strcmp(baseline, changed) == 0)
            return 1;
        materials.finalize_path = finalizer;
        materials.environment = environment;
        materials.environment_count = 2;
        materials.prune_strip_debug = 1;
        if (!cbs_build_fingerprint_with_context(
                "tests/fixtures/standalone-smoke.cbs", "x86_64", command_path,
                library_root,
                NULL, 0, &materials, changed) || strcmp(baseline, changed) == 0)
            return 1;
        environment[0].secret = 1;
        if (!cbs_build_fingerprint_with_context(
                "tests/fixtures/standalone-smoke.cbs", "x86_64", command_path,
                library_root,
                NULL, 0, &materials, baseline))
            return 1;
        environment[0].value = "changed-secret";
        if (!cbs_build_fingerprint_with_context(
                "tests/fixtures/standalone-smoke.cbs", "x86_64", command_path,
                library_root,
                NULL, 0, &materials, changed) || strcmp(baseline, changed) != 0)
            return 1;
    }
    /* The command and library paths are declared policy, so the key records
     * the paths themselves, and `fingerprint_append_tools` binds every
     * executable the recipe runs by digest.  The contents of a search path are
     * not material beyond that: hashing the whole path cost 56 s for
     * /usr/bin alone and no real host survived it, because one legitimate
     * setuid binary, hard link, or symlink pointing out of the directory
     * refused the walk and silently cost the build its entire fingerprint. */
    {
        char command_root[4256], library_root[4256];
        char header[4256], command[4256];
        char baseline[65], changed[65];
        char command_path[4256], other_path[4256];
        CbsFingerprintContext materials = {0};
        snprintf(command_root, sizeof(command_root), "%s/commands", root);
        snprintf(library_root, sizeof(library_root), "%s/libraries", root);
        snprintf(header, sizeof(header), "%s/tool.h", command_root);
        snprintf(command_path, sizeof(command_path), "%s", command_root);
        snprintf(other_path, sizeof(other_path), "%s:%s", command_root,
                 library_root);
        snprintf(command, sizeof(command), "%s/true", command_root);
        if (mkdir(command_root, 0700) != 0 || mkdir(library_root, 0700) != 0)
            return 1;
        file = fopen(command, "wb");
        if (file == NULL)
            return 1;
        fputs("tool", file);
        fclose(file);
        if (chmod(command, 0755) != 0)
            return 1;
        file = fopen(header, "wb");
        if (file == NULL)
            return 1;
        fputs("#define TOOL 1\n", file);
        fclose(file);
        if (!cbs_build_fingerprint_with_context(
                "tests/fixtures/standalone-smoke.cbs", "x86_64", command_path,
                library_root, NULL, 0, &materials, baseline))
            return 1;
        /* Editing a file on the search path leaves the key alone. */
        file = fopen(header, "wb");
        if (file == NULL)
            return 1;
        fputs("#define TOOL 2\n", file);
        fclose(file);
        if (!cbs_build_fingerprint_with_context(
                "tests/fixtures/standalone-smoke.cbs", "x86_64", command_path,
                library_root, NULL, 0, &materials, changed) ||
            strcmp(baseline, changed) != 0)
            return 1;
        /* Declaring a different search path does not. */
        if (!cbs_build_fingerprint_with_context(
                "tests/fixtures/standalone-smoke.cbs", "x86_64", other_path,
                library_root, NULL, 0, &materials, changed) ||
            strcmp(baseline, changed) == 0)
            return 1;
        /* A command path that resolves none of the recipe's commands still
         * refuses a key: every executable the build runs is bound by digest,
         * which is what the walk was redundantly duplicating. */
        if (cbs_build_fingerprint_with_context(
                "tests/fixtures/standalone-smoke.cbs", "x86_64",
                "/tmp/cbs-fingerprint-root-does-not-exist", library_root,
                NULL, 0, &materials, changed))
            return 1;
    }
    if (!flip_byte(build, 32) ||
        cbs_cixpkg_verify_tree(build, NULL, 0))
        return 1;
    if (!flip_byte(build, 32) ||
        !flip_byte(build, 400) ||
        cbs_cixpkg_verify_tree(build, NULL, 0))
        return 1;
    if (!truncate_file(build, 351) ||
        cbs_cixpkg_verify_tree(build, NULL, 0))
        return 1;

    /* Identity occupies bytes 160-223 and must never reach the flags byte at
     * 224: a 64-byte identity round trips, and a longer one is refused at
     * write time rather than silently truncated or written over the flags. */
    {
        char manifest[4256], package[4256];
        char longest[65];
        char excessive[66];
        char read_back[129];
        snprintf(manifest, sizeof(manifest), "%s/identity.manifest", root);
        snprintf(package, sizeof(package), "%s/identity.cixpkg", root);
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
    /* Every executable the recipe runs is bound by digest, and an
     * embedder-supplied verified identity set binds the toolchain without
     * resolving anything on the host. */
    {
        char command_root[4256], absent_root[4256], command[4256];
        char command_path[4256], library_path[4256];
        char baseline[65], changed[65];
        char gcc_digest[] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
        char binutils_digest[] = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
        CbsToolIdentity identities[2] = {
            {"gcc", "14.2", gcc_digest}, {"binutils", "2.43", binutils_digest}};
        CbsFingerprintContext materials = {0};
        snprintf(command_root, sizeof(command_root), "%s/absent-commands", root);
        snprintf(absent_root, sizeof(absent_root), "%s/absent-libraries", root);
        snprintf(command, sizeof(command), "%s/true", command_root);
        snprintf(command_path, sizeof(command_path), "%s", command_root);
        snprintf(library_path, sizeof(library_path), "%s", absent_root);
        if (mkdir(command_root, 0700) != 0)
            return 1;
        file = fopen(command, "wb");
        if (file == NULL)
            return 1;
        fputs("tool", file);
        fclose(file);
        if (chmod(command, 0755) != 0 || mkdir(absent_root, 0700) != 0 ||
            !cbs_build_fingerprint_with_context(
                "tests/fixtures/standalone-smoke.cbs", "x86_64", command_path,
                library_path, NULL, 0, &materials, baseline))
            return 1;
        /* Replacing the resolved command changes the key: that is how the
         * toolchain is bound, not by hashing the directory it sits in. */
        file = fopen(command, "wb");
        if (file == NULL)
            return 1;
        fputs("different tool", file);
        fclose(file);
        if (!cbs_build_fingerprint_with_context(
                "tests/fixtures/standalone-smoke.cbs", "x86_64", command_path,
                library_path, NULL, 0, &materials, changed) ||
            strcmp(baseline, changed) == 0)
            return 1;
        materials.tool_identities = identities;
        materials.tool_identity_count = 2;
        if (!cbs_build_fingerprint_with_context(
                "tests/fixtures/standalone-smoke.cbs", "x86_64",
                "/missing-command-root", "/missing-library-root", NULL, 0,
                &materials, baseline))
            return 1;
        {
            CbsToolIdentity swapped[2] = {identities[1], identities[0]};
            materials.tool_identities = swapped;
            if (!cbs_build_fingerprint_with_context(
                    "tests/fixtures/standalone-smoke.cbs", "x86_64",
                    "/missing-command-root", "/missing-library-root", NULL, 0,
                    &materials, changed) || strcmp(baseline, changed) != 0)
                return 1;
        }
        gcc_digest[0] = 'c';
        materials.tool_identities = identities;
        if (!cbs_build_fingerprint_with_context(
                "tests/fixtures/standalone-smoke.cbs", "x86_64",
                "/missing-command-root", "/missing-library-root", NULL, 0,
                &materials, changed) || strcmp(baseline, changed) == 0)
            return 1;
    }
    return 0;
}

/* Run the CIXPKG checks under a private root and remove it either way. */
int main(void) {
    char root[4096];
    int result;
    if (!test_temp_root(root, sizeof(root), "cbs-cixpkg"))
        return 1;
    result = run_test(root);
    test_remove_tree(root);
    if (result != 0)
        return result;
    puts("CIXPKG tests: PASS (writer, reader, pipeline, identity bounds, and "
         "corruption gates)");
    return 0;
}
