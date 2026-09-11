/* Command-line entry point and non-executing recipe inspection commands. */
#include "cbs.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef CBS_VERSION
#define CBS_VERSION "unknown"
#endif

/* Check the command-line recipe extension before parsing. */
static int has_cbs_extension(const char *path) {
    size_t length = strlen(path);
    return length >= 4 && strcmp(path + length - 4, ".cbs") == 0;
}

/* Read and normalize one recipe file for the lexer. */
static char *read_file(const char *path, size_t *length) {
    FILE *file;
    long size;
    char *source;
    size_t read_length;

    file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(
            stderr,
            "%s:1:1: error[CPDL-E1001]: lex: cannot read recipe; errno=%d\n",
            path, errno);
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fprintf(
            stderr,
            "%s:1:1: error[CPDL-E1001]: lex: cannot measure recipe; errno=%d\n",
            path, errno);
        fclose(file);
        return NULL;
    }
    source = cbs_allocate((size_t)size + 1);
    read_length = fread(source, 1, (size_t)size, file);
    if (read_length != (size_t)size || ferror(file)) {
        fprintf(stderr,
                "%s:1:1: error[CPDL-E1001]: lex: cannot read complete recipe; "
                "errno=%d\n",
                path, errno);
        fclose(file);
        free(source);
        return NULL;
    }
    if (fclose(file) != 0) {
        fprintf(
            stderr,
            "%s:1:1: error[CPDL-E1001]: lex: cannot close recipe; errno=%d\n",
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

/* Implement the validate/check command without executing a recipe. */
static int validate_file(const char *path) {
    char *source;
    size_t length;
    CbsTokenList tokens;
    CbsNode *document;
    int valid;

    memset(&tokens, 0, sizeof(tokens));
    if (!has_cbs_extension(path)) {
        fprintf(stderr,
                "%s:1:1: error[CPDL-E3004]: validation: recipe must use the "
                ".cbs extension\n",
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

/* Print the command-line interface summary. */
static void usage(FILE *stream) {
    fputs(
        "usage: cbs <command> [options]\n"
        "\ncbs - Cix Build System package engine (CPDL 0.1)\n\n"
        "commands:\n"
        "  cbs check RECIPE.cbs                 Validate without executing\n"
        "  cbs validate RECIPE.cbs [--json]     Alias for check\n"
        "  cbs explain RECIPE.cbs [--json]       Show the execution plan\n"
        "  cbs inspect RECIPE.cbs [ARTIFACT]    Show digest metadata\n"
        "  cbs build RECIPE.cbs --arch ARCH --staged ROOT [--output FILE] "
        "[--cache DIR] [--ca-file FILE]\n"
        "  cbs verify ARTIFACT.cixpkg           Verify an artifact alone\n"
        "  cbs extract ARTIFACT.cixpkg --into DIR Extract a verified artifact\n"
        "  cbs --help                           Show this help\n"
        "  cbs --version                        Show version\n",
        stream);
}

typedef struct {
    const char *recipe;
    const char *architecture;
    const char *staged;
    const char *output;
    const char *cache;
    const char *ca_file;
} CbsBuildOptions;

/* Parse build options independently of their order on the command line. */
static int parse_build_options(int argc, char **argv, CbsBuildOptions *options) {
    int index;
    memset(options, 0, sizeof(*options));
    if (argc < 3 || strcmp(argv[1], "build") != 0) {
        fprintf(stderr, "build: missing recipe\n");
        return 0;
    }
    options->recipe = argv[2];
    for (index = 3; index < argc; ++index) {
        const char *argument = argv[index];
        const char *value = NULL;
        if (strncmp(argument, "--arch=", 7) == 0)
            value = argument + 7;
        else if (strncmp(argument, "--staged=", 9) == 0)
            value = argument + 9;
        else if (strncmp(argument, "--output=", 9) == 0)
            value = argument + 9;
        else if (strncmp(argument, "--cache=", 8) == 0)
            value = argument + 8;
        else if (strncmp(argument, "--ca-file=", 10) == 0)
            value = argument + 10;
        else if (strcmp(argument, "--arch") == 0 ||
                 strcmp(argument, "--staged") == 0 ||
                 strcmp(argument, "--output") == 0 ||
                 strcmp(argument, "--cache") == 0 ||
                 strcmp(argument, "--ca-file") == 0) {
            if (++index >= argc) {
                fprintf(stderr, "build: option `%s` requires a value\n",
                        argument);
                return 0;
            }
            value = argv[index];
        } else {
            fprintf(stderr, "build: unknown option `%s`\n", argument);
            return 0;
        }
        if (value == NULL || value[0] == '\0') {
            fprintf(stderr, "build: option `%s` requires a non-empty value\n",
                    argument);
            return 0;
        }
        if (strcmp(argument, "--arch") == 0 ||
            strncmp(argument, "--arch=", 7) == 0)
            options->architecture = value;
        else if (strcmp(argument, "--staged") == 0 ||
                 strncmp(argument, "--staged=", 9) == 0)
            options->staged = value;
        else if (strcmp(argument, "--output") == 0 ||
                 strncmp(argument, "--output=", 9) == 0)
            options->output = value;
        else if (strcmp(argument, "--cache") == 0 ||
                 strncmp(argument, "--cache=", 8) == 0)
            options->cache = value;
        else
            options->ca_file = value;
    }
    if (options->architecture == NULL || options->staged == NULL) {
        fprintf(stderr, "build: --arch and --staged are required\n");
        return 0;
    }
    return 1;
}

/* Verify one CIXPKG artifact and print its identity. */
static int verify_file(const char *path) {
    char identity[129];
    if (!cbs_cixpkg_verify_tree(path, identity, sizeof(identity))) {
        fprintf(stderr,
                "%s: error[CIXPKG-E4001]: artifact verification failed\n",
                path);
        return 4;
    }
    printf("%s: verified CIXPKG (identity=%s)\n", path, identity);
    return 0;
}

/* Print a JSON string with quotes and backslashes escaped. */
static void print_json_string(const char *value) {
    const unsigned char *cursor;
    if (value == NULL) {
        fputs("null", stdout);
        return;
    }
    putchar('"');
    for (cursor = (const unsigned char *)value; *cursor != '\0'; ++cursor) {
        if (*cursor == '"' || *cursor == '\\')
            putchar('\\');
        putchar(*cursor);
    }
    putchar('"');
}

/* Validate a recipe and print its execution metadata and plan. */
static int explain_file(const char *path, int json) {
    char *source;
    size_t length, index;
    CbsTokenList tokens = {0};
    CbsNode *document;
    CbsBuildPlan plan;
    CbsBuildMetadata metadata;

    if (!has_cbs_extension(path)) {
        fprintf(stderr,
                "%s:1:1: error[CPDL-E3004]: validation: recipe must use the "
                ".cbs extension\n",
                path);
        return 3;
    }
    source = read_file(path, &length);
    if (source == NULL)
        return 3;
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
    if (!cbs_build_metadata(document, &metadata)) {
        cbs_node_destroy(document);
        cbs_token_list_destroy(&tokens);
        free(source);
        return 3;
    }
    if (json) {
        const CbsNode *package = document->children[0];
        size_t item_index, child_index;
        const char *version = NULL;
        long release = 0;
        fputs("{\"name\":", stdout);
        print_json_string(package->value);
        for (item_index = 0; item_index < package->child_count; ++item_index) {
            if (package->children[item_index]->kind == CBS_NODE_VERSION)
                version = package->children[item_index]->value;
            if (package->children[item_index]->kind == CBS_NODE_RELEASE)
                release = package->children[item_index]->number;
        }
        fputs(",\"version\":", stdout);
        print_json_string(version);
        printf(",\"release\":%ld,\"architecture\":null,\"sources\":[", release);
        {
            int first_source = 1;
            for (item_index = 0; item_index < package->child_count;
                 ++item_index) {
                const CbsNode *sources = package->children[item_index];
                if (sources->kind != CBS_NODE_SOURCES)
                    continue;
                for (child_index = 0; child_index < sources->child_count;
                     ++child_index) {
                    const CbsNode *source = sources->children[child_index];
                    size_t url_index;
                    if (!first_source)
                        putchar(',');
                    first_source = 0;
                    fputs("{\"name\":", stdout);
                    print_json_string(source->value);
                    fputs(",\"urls\":[", stdout);
                    {
                        int first_url = 1;
                        for (url_index = 0; url_index < source->child_count;
                             ++url_index) {
                            const CbsNode *url = source->children[url_index];
                            if (url->kind != CBS_NODE_URL)
                                continue;
                            if (!first_url)
                                putchar(',');
                            first_url = 0;
                            print_json_string(url->value);
                        }
                    }
                    fputs("],\"sha256\":", stdout);
                    for (url_index = 0; url_index < source->child_count;
                         ++url_index)
                        if (source->children[url_index]->kind ==
                            CBS_NODE_SHA256)
                            print_json_string(
                                source->children[url_index]->value);
                    fputs("}", stdout);
                }
            }
        }
        fputs("],\"requires\":{", stdout);
        {
            int first_group = 1;
            for (item_index = 0; item_index < package->child_count;
                 ++item_index) {
                const CbsNode *
                    requires
                = package->children[item_index];
                if (requires->kind != CBS_NODE_REQUIRES)
                    continue;
                for (child_index = 0; child_index < requires->child_count;
                     ++child_index) {
                    const CbsNode *group =
                        requires
                        ->children[child_index];
                    size_t dep_index;
                    if (!first_group)
                        putchar(',');
                    first_group = 0;
                    print_json_string(group->name);
                    fputs(":{", stdout);
                    for (dep_index = 0; dep_index < group->child_count;
                         ++dep_index) {
                        const CbsNode *dep = group->children[dep_index];
                        size_t later;
                        int first_value = 1;
                        for (later = 0; later < dep_index; ++later)
                            if (strcmp(group->children[later]->name,
                                       dep->name) == 0)
                                break;
                        if (later != dep_index)
                            continue;
                        if (dep_index != 0)
                            putchar(',');
                        print_json_string(dep->name);
                        putchar(':');
                        fputs("[", stdout);
                        for (later = dep_index; later < group->child_count;
                             ++later) {
                            if (strcmp(group->children[later]->name,
                                       dep->name) != 0)
                                continue;
                            if (!first_value)
                                putchar(',');
                            first_value = 0;
                            print_json_string(group->children[later]->value);
                        }
                        fputs("]", stdout);
                    }
                    fputs("}", stdout);
                }
            }
        }
        fputs("},\"build_image\":", stdout);
        print_json_string(metadata.build_image);
        fputs(",\"upstream\":", stdout);
        print_json_string(metadata.upstream);
        fputs(",\"toolchain\":", stdout);
        print_json_string(metadata.toolchain);
        fputs(",\"toolchain_reason\":", stdout);
        print_json_string(metadata.toolchain_reason);
        printf(",\"capabilities\":%zu,\"phases\":[", metadata.capability_count);
        for (index = 0; index < plan.count; ++index)
            printf("%s{\"name\":\"%s\",\"operations\":%zu}",
                   index == 0 ? "" : ",", plan.phases[index]->name,
                   plan.phases[index]->child_count);
        puts("]}");
    } else {
        printf("%s: CPDL 0.1 execution plan (%zu phases)\n", path, plan.count);
        printf("metadata build_image=%s upstream=%s toolchain=%s "
               "capabilities=%zu\n",
               metadata.build_image == NULL ? "none" : metadata.build_image,
               metadata.upstream == NULL ? "none" : metadata.upstream,
               metadata.toolchain == NULL ? "none" : metadata.toolchain,
               metadata.capability_count);
        for (index = 0; index < plan.count; ++index)
            printf("%zu %s operations=%zu\n", index + 1,
                   plan.phases[index]->name, plan.phases[index]->child_count);
    }
    cbs_node_destroy(document);
    cbs_token_list_destroy(&tokens);
    free(source);
    return 0;
}

/* Build one recipe through the standalone package pipeline. */
static int build_file(const char *recipe, const char *architecture,
                      const char *staged, const char *output, const char *cache,
                      const char *ca_file) {
    struct stat status;
    CbsFetchService service;
    char fetch_error[256];
    memset(&service, 0, sizeof(service));
    if (cache != NULL &&
        (stat(cache, &status) != 0 || !S_ISDIR(status.st_mode))) {
        fprintf(stderr, "%s: cache directory is not accessible\n", cache);
        return 3;
    }
    if (ca_file != NULL &&
        (stat(ca_file, &status) != 0 || !S_ISREG(status.st_mode))) {
        fprintf(stderr, "%s: CA file is not accessible\n", ca_file);
        return 3;
    }
    /* Cache hits must work in a network-less image without libcurl. */
    (void)cbs_cli_fetch_service_with_ca(&service, fetch_error,
                                        sizeof(fetch_error), ca_file);
    if (!cbs_build_standalone_with_cache(recipe, staged, output, architecture,
                                         &service, cache)) {
        fprintf(stderr, "build failed: recipe, staged tree, or package output "
                        "was rejected\n");
        return 3;
    }
    if (output == NULL)
        printf("staged %s\n", staged);
    else
        printf("built %s\n", output);
    return 0;
}

/* Print recipe identity, source, and optional artifact digest metadata. */
static int inspect_file(const char *path, const char *artifact) {
    char *source;
    size_t length;
    char recipe_digest[65], artifact_digest[65];
    CbsTokenList tokens = {0};
    CbsNode *document;
    CbsSourceSet sources = {0};
    size_t i;
    source = read_file(path, &length);
    if (source == NULL)
        return 3;
    if (!cbs_lex(path, source, length, &tokens))
        return 3;
    document = cbs_parse(path, source, length, &tokens);
    if (document == NULL || !cbs_validate(document, path, source) ||
        !cbs_sources_from_document(document, &sources) ||
        !cbs_digest_text(source, length, recipe_digest))
        return 3;
    printf("recipe-digest %s\n", recipe_digest);
    for (i = 0; i < sources.count; ++i)
        printf("source-digest %s %s\n", sources.items[i].name,
               sources.items[i].sha256);
    if (artifact != NULL && cbs_digest_file(artifact, artifact_digest))
        printf("artifact-digest %s\n", artifact_digest);
    cbs_source_set_destroy(&sources);
    cbs_node_destroy(document);
    cbs_token_list_destroy(&tokens);
    free(source);
    return 0;
}

/* Dispatch the command-line request selected by the user. */
int main(int argc, char **argv) {
    if (argc == 2 &&
        (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        usage(stdout);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        printf("cbs %s\n", CBS_VERSION);
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "verify") == 0)
        return verify_file(argv[2]);
    if (argc == 3 && strcmp(argv[1], "explain") == 0)
        return explain_file(argv[2], 0);
    if (argc == 4 && strcmp(argv[1], "explain") == 0 &&
        strcmp(argv[3], "--json") == 0)
        return explain_file(argv[2], 1);
    if (argc == 4 &&
        (strcmp(argv[1], "check") == 0 || strcmp(argv[1], "validate") == 0) &&
        strcmp(argv[3], "--json") == 0) {
        cbs_diagnostic_set_json(1);
        return validate_file(argv[2]);
    }
    if (argc == 5 && strcmp(argv[1], "extract") == 0 &&
        strcmp(argv[3], "--into") == 0) {
        if (!cbs_cixpkg_extract(argv[2], argv[4])) {
            fprintf(stderr,
                    "%s: error[CIXPKG-E4001]: artifact extraction failed\n",
                    argv[2]);
            return 4;
        }
        printf("extracted %s\n", argv[4]);
        return 0;
    }
    if (argc >= 3 && strcmp(argv[1], "build") == 0) {
        CbsBuildOptions options;
        if (!parse_build_options(argc, argv, &options))
            return 2;
        return build_file(options.recipe, options.architecture, options.staged,
                          options.output, options.cache, options.ca_file);
    }
    if (argc == 3 && strcmp(argv[1], "inspect") == 0)
        return inspect_file(argv[2], NULL);
    if (argc == 4 && strcmp(argv[1], "inspect") == 0)
        return inspect_file(argv[2], argv[3]);
    if (argc != 3 ||
        (strcmp(argv[1], "validate") != 0 && strcmp(argv[1], "check") != 0)) {
        usage(stderr);
        return 2;
    }
    return validate_file(argv[2]);
}
