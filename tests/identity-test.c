/* Regression tests for canonical package identity formatting. */
#include "cbs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_source(const char *path, size_t *length) {
    FILE *file = fopen(path, "rb");
    long size;
    char *source;
    if (file == NULL || fseek(file, 0, SEEK_END) != 0 ||
        (size = ftell(file)) < 0 || fseek(file, 0, SEEK_SET) != 0)
        return NULL;
    source = cbs_allocate((size_t)size + 1);
    if (fread(source, 1, (size_t)size, file) != (size_t)size) {
        fclose(file);
        free(source);
        return NULL;
    }
    fclose(file);
    source[size] = '\0';
    *length = (size_t)size;
    return source;
}

int main(int argc, char **argv) {
    static const char expected[] = "gcc-16.2.0-11-x86_64";
    static const char expected_metadata[] = "identity=gcc-16.2.0-11-x86_64\n";
    char *source;
    size_t source_length;
    CbsTokenList tokens;
    CbsNode *document;
    CbsPackageIdentity identity;
    CbsExecutionContext context;
    char *canonical;
    char *filename;
    char *metadata;
    char *interpolated;
    size_t metadata_length;
    int result = 1;

    memset(&tokens, 0, sizeof(tokens));
    memset(&context, 0, sizeof(context));
    if (argc != 2) {
        fputs("usage: identity-test PACKAGE.cbs\n", stderr);
        return 2;
    }
    source = read_source(argv[1], &source_length);
    if (source == NULL || !cbs_lex(argv[1], source, source_length, &tokens))
        return 1;
    document = cbs_parse(argv[1], source, source_length, &tokens);
    if (document == NULL || !cbs_validate(document, argv[1], source))
        goto cleanup;
    if (!cbs_identity_from_document(document, "x86_64", &identity) ||
        cbs_identity_from_document(document, "any", &identity) ||
        cbs_identity_from_document(document, "X86_64", &identity) ||
        cbs_identity_from_document(document, "../x86_64", &identity))
        goto cleanup;
    if (!cbs_identity_from_document(document, "x86_64", &identity))
        goto cleanup;
    canonical = cbs_identity_string(&identity);
    filename = cbs_identity_artifact_filename(&identity);
    metadata = cbs_identity_digest_metadata(&identity, &metadata_length);
    if (canonical == NULL || filename == NULL || metadata == NULL ||
        strcmp(canonical, expected) != 0 ||
        strcmp(filename, "gcc-16.2.0-11-x86_64.cixpkg") != 0 ||
        metadata_length != sizeof(expected_metadata) - 1 ||
        memcmp(metadata, expected_metadata, metadata_length) != 0) {
        free(canonical);
        free(filename);
        free(metadata);
        goto cleanup;
    }
    cbs_identity_apply_execution_context(&identity, &context);
    interpolated = cbs_resolve_value("${name}-${version}-${release}-${arch}",
                                     CBS_TOKEN_STRING, &context);
    if (strcmp(interpolated, expected) != 0) {
        free(interpolated);
        free(canonical);
        free(filename);
        free(metadata);
        goto cleanup;
    }
    free(interpolated);
    free(canonical);
    free(filename);
    free(metadata);
    result = 0;
cleanup:
    cbs_node_destroy(document);
    cbs_token_list_destroy(&tokens);
    free(source);
    if (result != 0) {
        fputs("package identity tests: FAIL\n", stderr);
        return 1;
    }
    puts("package identity tests: PASS (one canonical identity)");
    return 0;
}
