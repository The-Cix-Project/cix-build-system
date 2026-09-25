/* Command-line entry point and non-executing recipe inspection commands. */
#define _POSIX_C_SOURCE 200809L

#include "cbs.h"

#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

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

/* Report extraction failures against the object that actually failed. */
static int extract_file(const char *artifact, const char *destination) {
    char parent[4096];
    char *slash;
    struct stat status;

    if (destination == NULL || destination[0] == '\0' ||
        strlen(destination) > sizeof(parent) - 32) {
        fprintf(stderr,
                "%s: error[CIXPKG-E4002]: invalid extraction destination\n",
                destination == NULL ? "(null)" : destination);
        return 4;
    }
    if (lstat(destination, &status) == 0) {
        fprintf(stderr,
                "%s: error[CIXPKG-E4003]: extraction destination already "
                "exists\n",
                destination);
        return 4;
    }
    if (errno != ENOENT) {
        fprintf(stderr,
                "%s: error[CIXPKG-E4002]: cannot inspect extraction "
                "destination; errno=%d\n",
                destination, errno);
        return 4;
    }
    if (snprintf(parent, sizeof(parent), "%s", destination) >=
        (int)sizeof(parent)) {
        fprintf(stderr,
                "%s: error[CIXPKG-E4002]: invalid extraction destination\n",
                destination);
        return 4;
    }
    slash = strrchr(parent, '/');
    if (slash == NULL) {
        strcpy(parent, ".");
    } else if (slash == parent) {
        parent[1] = '\0';
    } else {
        *slash = '\0';
    }
    if (stat(parent, &status) != 0) {
        if (errno == ENOENT)
            fprintf(stderr,
                    "%s: error[CIXPKG-E4004]: extraction destination "
                    "parent does not exist\n",
                    destination);
        else
            fprintf(stderr,
                    "%s: error[CIXPKG-E4002]: extraction destination "
                    "parent is unavailable; errno=%d\n",
                    destination, errno);
        return 4;
    }
    if (!S_ISDIR(status.st_mode)) {
        fprintf(stderr,
                "%s: error[CIXPKG-E4002]: extraction destination parent is "
                "not a directory\n",
                destination);
        return 4;
    }
    if (!cbs_cixpkg_verify_tree(artifact, NULL, 0)) {
        fprintf(stderr,
                "%s: error[CIXPKG-E4001]: artifact verification failed\n",
                artifact);
        return 4;
    }
    if (!cbs_cixpkg_extract(artifact, destination)) {
        fprintf(stderr,
                "%s: error[CIXPKG-E4002]: could not populate extraction "
                "destination\n",
                destination);
        return 4;
    }
    printf("extracted %s\n", destination);
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
        "[--cache DIR] [--ca-file FILE] [--events human|jsonl] "
        "[--finalize-command CMD] [--prune-policy FILE] [--firmware-root DIR]\n"
        "      [--input NAME=FILE ...]\n"
        "      [--command-path DIRS]\n"
        "      [--library-path DIRS]\n"
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
    const char *events;
    const char *finalize_command;
    const char *prune_policy;
    const char *firmware_root;
    const char *command_path;
    const char *library_path;
    CbsInputBinding inputs[32];
    size_t input_count;
} CbsBuildOptions;

static int valid_input_name(const char *name) {
    size_t index;
    if (name == NULL || name[0] == '\0' ||
        !(isalpha((unsigned char)name[0]) || name[0] == '_'))
        return 0;
    for (index = 1; name[index] != '\0'; ++index)
        if (!(isalnum((unsigned char)name[index]) || name[index] == '_'))
            return 0;
    return 1;
}

static int add_input(CbsBuildOptions *options, const char *spec) {
    const char *separator = strchr(spec, '=');
    char *name;
    int valid;
    if (options->input_count == 32 || separator == NULL || separator == spec ||
        separator[1] == '\0' || separator[1] != '/') {
        fprintf(stderr,
                "build: --input requires NAME=ABSOLUTE_FILE with a portable name\n");
        return 0;
    }
    name = cbs_duplicate_range(spec, (size_t)(separator - spec));
    valid = valid_input_name(name);
    if (!valid) {
        free(name);
        fprintf(stderr,
                "build: --input requires NAME=ABSOLUTE_FILE with a portable name\n");
        return 0;
    }
    for (size_t index = 0; index < options->input_count; ++index) {
        if (strcmp(options->inputs[index].name, name) == 0) {
            fprintf(stderr, "build: duplicate --input name `%s`\n", name);
            free(name);
            return 0;
        }
    }
    options->inputs[options->input_count].name = name;
    options->inputs[options->input_count].path = cbs_duplicate(separator + 1);
    ++options->input_count;
    return 1;
}

static void free_inputs(CbsBuildOptions *options) {
    size_t index;
    for (index = 0; index < options->input_count; ++index) {
        free((void *)options->inputs[index].name);
        free((void *)options->inputs[index].path);
    }
}

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
        if (strncmp(argument, "--input=", 8) == 0) {
            if (!add_input(options, argument + 8))
                return 0;
            continue;
        }
        if (strcmp(argument, "--input") == 0) {
            if (++index >= argc || !add_input(options, argv[index])) {
                if (index >= argc)
                    fprintf(stderr, "build: option `--input` requires a value\n");
                return 0;
            }
            continue;
        }
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
        else if (strncmp(argument, "--events=", 9) == 0)
            value = argument + 9;
        else if (strncmp(argument, "--finalize-command=", 19) == 0)
            value = argument + 19;
        else if (strncmp(argument, "--prune-policy=", 15) == 0)
            value = argument + 15;
        else if (strncmp(argument, "--firmware-root=", 16) == 0)
            value = argument + 16;
        else if (strncmp(argument, "--command-path=", 15) == 0)
            value = argument + 15;
        else if (strncmp(argument, "--library-path=", 15) == 0)
            value = argument + 15;
        else if (strcmp(argument, "--arch") == 0 ||
                 strcmp(argument, "--staged") == 0 ||
                 strcmp(argument, "--output") == 0 ||
                 strcmp(argument, "--cache") == 0 ||
                 strcmp(argument, "--ca-file") == 0 ||
                 strcmp(argument, "--events") == 0 ||
                 strcmp(argument, "--finalize-command") == 0 ||
                 strcmp(argument, "--prune-policy") == 0 ||
                 strcmp(argument, "--firmware-root") == 0 ||
                 strcmp(argument, "--command-path") == 0) {
            if (++index >= argc) {
                fprintf(stderr, "build: option `%s` requires a value\n",
                        argument);
                return 0;
            }
            value = argv[index];
        } else if (strcmp(argument, "--library-path") == 0) {
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
        else if (strcmp(argument, "--ca-file") == 0 ||
                 strncmp(argument, "--ca-file=", 10) == 0)
            options->ca_file = value;
        else if (strcmp(argument, "--finalize-command") == 0 ||
                 strncmp(argument, "--finalize-command=", 19) == 0)
            options->finalize_command = value;
        else if (strcmp(argument, "--prune-policy") == 0 ||
                 strncmp(argument, "--prune-policy=", 15) == 0)
            options->prune_policy = value;
        else if (strcmp(argument, "--firmware-root") == 0 ||
                 strncmp(argument, "--firmware-root=", 16) == 0)
            options->firmware_root = value;
        else if (strcmp(argument, "--command-path") == 0 ||
                 strncmp(argument, "--command-path=", 15) == 0)
            options->command_path = value;
        else if (strcmp(argument, "--library-path") == 0 ||
                 strncmp(argument, "--library-path=", 15) == 0)
            options->library_path = value;
        else
            options->events = value;
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

/* Count expanded operations: a list (a `for` or `each` expansion) counts
 * what it contains, recursively. */
static size_t count_plan_operations(const CbsNode *block) {
    size_t total = 0;
    size_t index;
    for (index = 0; index < block->child_count; ++index)
        total += block->children[index]->kind == CBS_NODE_LIST
                     ? count_plan_operations(block->children[index])
                     : 1;
    return total;
}

static const CbsNode *explained_tools(const CbsNode *document) {
    const CbsNode *package = document->children[0];
    size_t index;
    for (index = 0; index < package->child_count; ++index)
        if (package->children[index]->kind == CBS_NODE_TOOLS)
            return package->children[index];
    return NULL;
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
        const char *format = NULL;
        long release = 0;
        fputs("{\"name\":", stdout);
        print_json_string(package->value);
        for (item_index = 0; item_index < package->child_count; ++item_index) {
            if (package->children[item_index]->kind == CBS_NODE_VERSION)
                version = package->children[item_index]->value;
            if (package->children[item_index]->kind == CBS_NODE_RELEASE)
                release = package->children[item_index]->number;
            if (package->children[item_index]->kind == CBS_NODE_FORMAT)
                format = package->children[item_index]->value;
        }
        fputs(",\"version\":", stdout);
        print_json_string(version);
        printf(",\"release\":%ld,\"format\":", release);
        print_json_string(format);
        fputs(",\"architecture\":null,\"sources\":[", stdout);
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
        fputs("},\"license\":", stdout);
        print_json_string(metadata.license);
        fputs(",\"build_image\":", stdout);
        print_json_string(metadata.build_image);
        fputs(",\"upstream\":", stdout);
        print_json_string(metadata.upstream);
        fputs(",\"toolchain\":", stdout);
        print_json_string(metadata.toolchain);
        fputs(",\"toolchain_reason\":", stdout);
        print_json_string(metadata.toolchain_reason);
        fputs(",\"capabilities\":[", stdout);
        {
            const CbsNode *package = document->children[0];
            int first_capability = 1;
            for (index = 0; index < package->child_count; ++index) {
                const CbsNode *item = package->children[index];
                if (item->kind != CBS_NODE_CAPABILITY)
                    continue;
                if (!first_capability)
                    putchar(',');
                first_capability = 0;
                print_json_string(item->value);
            }
        }
        fputs("],\"metadata\":{", stdout);
        {
            const CbsNode *package = document->children[0];
            size_t metadata_index;
            int first_metadata = 1;
            for (metadata_index = 0; metadata_index < package->child_count;
                 ++metadata_index) {
                const CbsNode *item = package->children[metadata_index];
                size_t property_index;
                if (item->kind != CBS_NODE_METADATA)
                    continue;
                for (property_index = 0; property_index < item->child_count;
                     ++property_index) {
                    const CbsNode *property = item->children[property_index];
                    if (!first_metadata)
                        putchar(',');
                    first_metadata = 0;
                    print_json_string(property->name);
                    putchar(':');
                    print_json_string(property->value);
                }
            }
        }
        fputs("},\"tools\":[", stdout);
        {
            const CbsNode *tools = explained_tools(document);
            int first_tool = 1;
            if (tools != NULL) {
                for (child_index = 0; child_index < tools->child_count;
                     ++child_index) {
                    const CbsNode *tool = tools->children[child_index];
                    if (!first_tool)
                        putchar(',');
                    first_tool = 0;
                    fputs("{\"kind\":", stdout);
                    print_json_string(tool->name);
                    fputs(",\"policy\":", stdout);
                    print_json_string("alias");
                    fputs(",\"source\":", stdout);
                    print_json_string(tool->value);
                    putchar('}');
                }
            }
        }
        fputs("],\"command_path\":", stdout);
        print_json_string(CBS_DEFAULT_COMMAND_PATH);
        fputs(",\"library_path\":", stdout);
        print_json_string(CBS_DEFAULT_LIBRARY_PATH);
        fputs(",\"phases\":[", stdout);
        for (index = 0; index < plan.count; ++index)
            printf("%s{\"name\":\"%s\",\"operations\":%zu}",
                   index == 0 ? "" : ",", plan.phases[index]->name,
                   count_plan_operations(plan.phases[index]));
        puts("]}");
    } else {
        printf("%s: CPDL 0.1 execution plan (%zu phases)\n", path, plan.count);
        printf("metadata build_image=%s upstream=%s toolchain=%s "
               "capabilities=%zu\n",
               metadata.build_image == NULL ? "none" : metadata.build_image,
               metadata.upstream == NULL ? "none" : metadata.upstream,
               metadata.toolchain == NULL ? "none" : metadata.toolchain,
               metadata.capability_count);
        printf("command-path %s\n", CBS_DEFAULT_COMMAND_PATH);
        printf("library-path %s\n", CBS_DEFAULT_LIBRARY_PATH);
        {
            const CbsNode *tools = explained_tools(document);
            if (tools != NULL) {
                size_t tool_index;
                for (tool_index = 0; tool_index < tools->child_count;
                     ++tool_index) {
                    const CbsNode *tool = tools->children[tool_index];
                    printf("tool-policy %s alias %s\n", tool->name,
                           tool->value);
                }
            }
        }
        for (index = 0; index < plan.count; ++index)
            printf("%zu %s operations=%zu\n", index + 1,
                   plan.phases[index]->name,
                   count_plan_operations(plan.phases[index]));
    }
    cbs_node_destroy(document);
    cbs_token_list_destroy(&tokens);
    free(source);
    return 0;
}

typedef struct {
    char *resolved;
} CbsFinalizeCommand;

/* Resolve a finalizer against the approved command roots. */
static char *resolve_finalize_command(const char *command,
                                      const char *command_path) {
    const char *cursor;
    if (command == NULL || command[0] == '\0')
        return NULL;
    if (strchr(command, '/') != NULL)
        return access(command, X_OK) == 0 ? cbs_duplicate(command) : NULL;
    cursor = command_path == NULL ? CBS_DEFAULT_COMMAND_PATH : command_path;
    while (1) {
        const char *end = strchr(cursor, ':');
        size_t length = end == NULL ? strlen(cursor) : (size_t)(end - cursor);
        char *candidate = cbs_allocate(length + strlen(command) + 2);
        snprintf(candidate, length + strlen(command) + 2, "%.*s/%s",
                 (int)length, cursor, command);
        if (access(candidate, X_OK) == 0)
            return candidate;
        free(candidate);
        if (end == NULL)
            break;
        cursor = end + 1;
    }
    return NULL;
}

/* Run an explicit process-level finalizer against the staged root. */
static int run_finalize_command(const char *staged_root, void *user) {
    const CbsFinalizeCommand *policy = user;
    pid_t child;
    int status;
    if (policy == NULL || policy->resolved == NULL || staged_root == NULL)
        return 0;
    child = fork();
    if (child < 0)
        return 0;
    if (child == 0) {
        execl(policy->resolved, policy->resolved, staged_root, (char *)NULL);
        _exit(127);
    }
    if (waitpid(child, &status, 0) < 0)
        return 0;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

/* Build one recipe through the standalone package pipeline. */
static int build_file(const char *recipe, const char *architecture,
                      const char *staged, const char *output, const char *cache,
                      const char *ca_file, const char *events,
                      const char *finalize_command,
                      const char *prune_policy_path,
                      const char *firmware_root,
                      const char *command_path,
                      const char *library_path,
                      const CbsInputBinding *inputs, size_t input_count) {
    struct stat status;
    CbsFetchService service;
    CbsBuildEventSink event_sink = NULL;
    FILE *event_stream = stderr;
    int event_fd = -1;
    int result;
    char fetch_error[256];
    CbsPrunePolicy prune_policy;
    char prune_error[256];
    struct stat firmware_status;
    struct stat input_status;
    CbsFinalizeCommand finalize_policy = {0};
    memset(&service, 0, sizeof(service));
    if (command_path != NULL && !cbs_command_path_is_valid(command_path)) {
        fprintf(stderr, "build: --command-path must contain only non-empty "
                        "absolute directories without . or .. components\n");
        return 2;
    }
    if (library_path != NULL && !cbs_library_path_is_valid(library_path)) {
        fprintf(stderr, "build: --library-path must contain only non-empty "
                        "absolute directories without . or .. components\n");
        return 2;
    }
    if (finalize_command != NULL) {
        finalize_policy.resolved = resolve_finalize_command(
            finalize_command,
            command_path == NULL ? CBS_DEFAULT_COMMAND_PATH : command_path);
        if (finalize_policy.resolved == NULL) {
            fprintf(stderr, "build: finalize command is not executable under "
                            "the approved command-path policy\n");
            return 2;
        }
    }
    if (prune_policy_path != NULL &&
        !cbs_prune_policy_load(prune_policy_path, &prune_policy,
                               prune_error, sizeof(prune_error))) {
        fprintf(stderr, "build: %s\n", prune_error);
        return 2;
    }
    if (firmware_root != NULL &&
        (stat(firmware_root, &firmware_status) != 0 ||
         !S_ISDIR(firmware_status.st_mode))) {
        fprintf(stderr, "build: firmware root is not an accessible directory: %s\n",
                firmware_root);
        return 3;
    }
    for (size_t input_index = 0; input_index < input_count; ++input_index) {
        if (stat(inputs[input_index].path, &input_status) != 0 ||
            !S_ISREG(input_status.st_mode)) {
            fprintf(stderr, "build: input `%s` is not an accessible regular file: %s\n",
                    inputs[input_index].name, inputs[input_index].path);
            return 3;
        }
    }
    if (events != NULL) {
        if (strcmp(events, "human") == 0)
            event_sink = cbs_build_event_human;
        else if (strcmp(events, "jsonl") == 0)
            event_sink = cbs_build_event_jsonl;
        else {
            fprintf(stderr, "build: --events must be human or jsonl\n");
            return 2;
        }
        event_fd = dup(fileno(stderr));
        if (event_fd < 0 || (event_stream = fdopen(event_fd, "w")) == NULL) {
            if (event_fd >= 0)
                close(event_fd);
            fprintf(stderr, "build: cannot initialize event reporter\n");
            return 3;
        }
    }
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
    result = cbs_build_standalone_with_events_policy_path_inputs(
            recipe, staged, output, architecture, &service, cache,
            finalize_command == NULL ? NULL : run_finalize_command,
            finalize_command == NULL ? NULL : (void *)&finalize_policy,
            firmware_root,
            prune_policy_path == NULL ? NULL : &prune_policy, command_path,
            library_path, inputs, input_count, event_sink, event_stream);
    if (event_stream != stderr)
        fclose(event_stream);
    free(finalize_policy.resolved);
    if (!result) {
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
    CbsBuildMetadata metadata;
    size_t i;
    source = read_file(path, &length);
    if (source == NULL)
        return 3;
    if (!cbs_lex(path, source, length, &tokens))
        return 3;
    document = cbs_parse(path, source, length, &tokens);
    if (document == NULL || !cbs_validate(document, path, source) ||
        !cbs_sources_from_document(document, &sources) ||
        !cbs_digest_text(source, length, recipe_digest) ||
        !cbs_build_metadata(document, &metadata))
        return 3;
    printf("recipe-digest %s\n", recipe_digest);
    if (metadata.license != NULL)
        printf("license %s\n", metadata.license);
    for (i = 0; i < sources.count; ++i)
        printf("source-digest %s %s\n", sources.items[i].name,
               sources.items[i].sha256);
    if (artifact != NULL && cbs_digest_file(artifact, artifact_digest))
        printf("artifact-digest %s\n", artifact_digest);
    if (artifact != NULL) {
        char license[4096];
        if (!cbs_cixpkg_read_license(artifact, license, sizeof(license)))
            return 3;
        if (license[0] != '\0')
            printf("artifact-license %s\n", license);
    }
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
        return extract_file(argv[2], argv[4]);
    }
    if (argc >= 3 && strcmp(argv[1], "build") == 0) {
        CbsBuildOptions options;
        int result;
        if (!parse_build_options(argc, argv, &options))
            return 2;
        result = build_file(options.recipe, options.architecture, options.staged,
                          options.output, options.cache, options.ca_file,
                      options.events, options.finalize_command,
                          options.prune_policy, options.firmware_root,
                          options.command_path, options.library_path,
                          options.inputs, options.input_count);
        free_inputs(&options);
        return result;
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
