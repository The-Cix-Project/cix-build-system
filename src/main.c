#include "cbs.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    fputs("usage: cbs validate PACKAGE.cbs\n", stream);
}

int main(int argc, char **argv)
{
    if (argc == 2 && (strcmp(argv[1], "--help") == 0 ||
                      strcmp(argv[1], "-h") == 0)) {
        usage(stdout);
        return 0;
    }
    if (argc != 3 || strcmp(argv[1], "validate") != 0) {
        usage(stderr);
        return 2;
    }
    return validate_file(argv[2]);
}
