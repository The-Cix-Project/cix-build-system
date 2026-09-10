#include "cbs.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int has_cbs_extension(const char *path)
{
    size_t length = strlen(path);
    return length >= 4 && strcmp(path + length - 4, ".cbs") == 0;
}

static char *read_file(const char *path, size_t *length)
{
    FILE *file;
    long size;
    char *source;
    size_t read_length;

    file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "%s:1:1: error[CPDL-E1001]: lex: cannot read recipe; errno=%d\n",
                path, errno);
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fprintf(stderr, "%s:1:1: error[CPDL-E1001]: lex: cannot measure recipe; errno=%d\n",
                path, errno);
        fclose(file);
        return NULL;
    }
    source = cbs_allocate((size_t)size + 1);
    read_length = fread(source, 1, (size_t)size, file);
    if (read_length != (size_t)size || ferror(file)) {
        fprintf(stderr, "%s:1:1: error[CPDL-E1001]: lex: cannot read complete recipe; errno=%d\n",
                path, errno);
        fclose(file);
        free(source);
        return NULL;
    }
    if (fclose(file) != 0) {
        fprintf(stderr, "%s:1:1: error[CPDL-E1001]: lex: cannot close recipe; errno=%d\n",
                path, errno);
        free(source);
        return NULL;
    }
    source[read_length] = '\0';
    {
        size_t input = 0;
        size_t output = 0;
        while (input < read_length) {
            if (source[input] == '\r' && input + 1 < read_length &&
                source[input + 1] == '\n') {
                source[output++] = '\n';
                input += 2;
            } else {
                source[output++] = source[input++];
            }
        }
        source[output] = '\0';
        *length = output;
    }
    return source;
}

static int validate_file(const char *path)
{
    char *source;
    size_t length;
    CbsTokenList tokens;
    CbsNode *document;
    int valid;

    memset(&tokens, 0, sizeof(tokens));
    if (!has_cbs_extension(path)) {
        fprintf(stderr,
                "%s:1:1: error[CPDL-E3004]: validation: recipe must use the .cbs extension\n",
                path);
        return 3;
    }
    source = read_file(path, &length);
    if (source == NULL)
        return 3;
    if (!cbs_lex(path, source, length, &tokens)) {
        cbs_token_list_destroy(&tokens);
        free(source);
        return 3;
    }
    document = cbs_parse(path, source, length, &tokens);
    if (document == NULL) {
        cbs_token_list_destroy(&tokens);
        free(source);
        return 3;
    }
    valid = cbs_validate(document, path, source);
    cbs_node_destroy(document);
    cbs_token_list_destroy(&tokens);
    free(source);
    if (!valid)
        return 3;
    printf("%s: valid CPDL 0.1\n", path);
    return 0;
}

static void usage(FILE *stream)
{
    fputs("usage: cbs validate PACKAGE.cbs\n"
          "\ncbs - Cix Build System package engine (CPDL 0.1)\n\n"
          "commands:\n"
          "  cbs check RECIPE.cbs                 Validate without executing\n"
          "  cbs validate RECIPE.cbs              Alias for check\n"
          "  cbs explain RECIPE.cbs               Show the execution plan\n"
          "  cbs inspect RECIPE.cbs [ARTIFACT]    Show digest metadata\n"
          "  cbs build RECIPE.cbs --arch ARCH --staged ROOT --output FILE [--cache DIR]\n"
          "  cbs verify ARTIFACT.cixpkg           Verify an artifact alone\n"
          "  cbs extract ARTIFACT.cixpkg --into DIR Extract a verified artifact\n"
          "  cbs --help                           Show this help\n"
          "  cbs --version                        Show version\n", stream);
}

static int verify_file(const char *path)
{
    char identity[129];
    if (!cbs_cixpkg_verify_tree(path, identity, sizeof(identity))) {
        fprintf(stderr, "%s: error[CIXPKG-E4001]: artifact verification failed\n", path);
        return 4;
    }
    printf("%s: verified CIXPKG (identity=%s)\n", path, identity);
    return 0;
}

static int explain_file(const char *path)
{
    char *source;
    size_t length, index;
    CbsTokenList tokens = {0};
    CbsNode *document;
    CbsBuildPlan plan;

    if (!has_cbs_extension(path)) {
        fprintf(stderr,
                "%s:1:1: error[CPDL-E3004]: validation: recipe must use the .cbs extension\n",
                path);
        return 3;
    }
    source = read_file(path, &length);
    if (source == NULL) return 3;
    if (!cbs_lex(path, source, length, &tokens)) {
        free(source);
        cbs_token_list_destroy(&tokens);
        return 3;
    }
    document = cbs_parse(path, source, length, &tokens);
    if (document == NULL || !cbs_validate(document, path, source) ||
        !cbs_build_plan(document, &plan)) {
        cbs_node_destroy(document);
        cbs_token_list_destroy(&tokens);
        free(source);
        return 3;
    }
    printf("%s: CPDL 0.1 execution plan (%zu phases)\n", path, plan.count);
    for (index = 0; index < plan.count; ++index)
        printf("%zu %s operations=%zu\n", index + 1,
               plan.phases[index]->name, plan.phases[index]->child_count);
    cbs_node_destroy(document);
    cbs_token_list_destroy(&tokens);
    free(source);
    return 0;
}

static int build_file(const char *recipe, const char *architecture,
                      const char *staged, const char *output,
                      const char *cache)
{
    struct stat status;
    CbsFetchService service;
    char fetch_error[256];
    if (cache != NULL && (stat(cache, &status) != 0 || !S_ISDIR(status.st_mode))) {
        fprintf(stderr, "%s: cache directory is not accessible\n", cache);
        return 3;
    }
    if (!cbs_cli_fetch_service(&service, fetch_error, sizeof(fetch_error))) {
        fprintf(stderr, "source transport unavailable: %s\n", fetch_error);
        return 3;
    }
    if (!cbs_build_standalone_with_cache(recipe, staged, output, architecture,
                                         &service, cache)) {
        fprintf(stderr, "build failed: recipe, staged tree, or package output was rejected\n");
        return 3;
    }
    printf("built %s\n", output);
    return 0;
}

static int inspect_file(const char *path, const char *artifact)
{
    char *source; size_t length; char recipe_digest[65], artifact_digest[65];
    CbsTokenList tokens = {0}; CbsNode *document; CbsSourceSet sources = {0}; size_t i;
    source = read_file(path, &length); if (source == NULL) return 3;
    if (!cbs_lex(path, source, length, &tokens)) return 3;
    document = cbs_parse(path, source, length, &tokens);
    if (document == NULL || !cbs_validate(document, path, source) ||
        !cbs_sources_from_document(document, &sources) ||
        !cbs_digest_text(source, length, recipe_digest)) return 3;
    printf("recipe-digest %s\n", recipe_digest);
    for (i=0;i<sources.count;++i) printf("source-digest %s %s\n", sources.items[i].name, sources.items[i].sha256);
    if (artifact != NULL && cbs_digest_file(artifact, artifact_digest)) printf("artifact-digest %s\n", artifact_digest);
    cbs_source_set_destroy(&sources); cbs_node_destroy(document); cbs_token_list_destroy(&tokens); free(source); return 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && (strcmp(argv[1], "--help") == 0 ||
                      strcmp(argv[1], "-h") == 0)) {
        usage(stdout);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        puts("cbs 0.1.0");
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "verify") == 0)
        return verify_file(argv[2]);
    if (argc == 3 && strcmp(argv[1], "explain") == 0)
        return explain_file(argv[2]);
    if (argc == 5 && strcmp(argv[1], "extract") == 0 &&
        strcmp(argv[3], "--into") == 0) {
        if (!cbs_cixpkg_extract(argv[2], argv[4])) {
            fprintf(stderr, "%s: error[CIXPKG-E4001]: artifact extraction failed\n",
                    argv[2]);
            return 4;
        }
        printf("extracted %s\n", argv[4]);
        return 0;
    }
    if (argc == 9 && strcmp(argv[1], "build") == 0 &&
        strcmp(argv[3], "--arch") == 0 && strcmp(argv[5], "--staged") == 0 &&
        strcmp(argv[7], "--output") == 0)
        return build_file(argv[2], argv[4], argv[6], argv[8], NULL);
    if (argc == 11 && strcmp(argv[1], "build") == 0 &&
        strcmp(argv[3], "--arch") == 0 && strcmp(argv[5], "--staged") == 0 &&
        strcmp(argv[7], "--output") == 0 && strcmp(argv[9], "--cache") == 0)
        return build_file(argv[2], argv[4], argv[6], argv[8], argv[10]);
    if (argc == 3 && strcmp(argv[1], "inspect") == 0) return inspect_file(argv[2], NULL);
    if (argc == 4 && strcmp(argv[1], "inspect") == 0) return inspect_file(argv[2], argv[3]);
    if (argc != 3 || (strcmp(argv[1], "validate") != 0 &&
                      strcmp(argv[1], "check") != 0)) {
        usage(stderr);
        return 2;
    }
    return validate_file(argv[2]);
}
