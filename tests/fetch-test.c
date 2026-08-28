#define _POSIX_C_SOURCE 200809L
#include "cbs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct { int calls; } FetchState;

static int fetch_mock(const char *url, const char *destination, void *opaque,
                      char *error, size_t error_size)
{
    FetchState *state = opaque;
    FILE *file;
    const char *value = strstr(url, "bad.invalid") != NULL ? "wrong" :
                        (strstr(url, "empty") != NULL ? "" : "abc");
    state->calls += 1;
    file = fopen(destination, "wb");
    if (file == NULL) {
        snprintf(error, error_size, "destination could not be opened");
        return 0;
    }
    if (fwrite(value, 1, strlen(value), file) != strlen(value) ||
        fclose(file) != 0) {
        snprintf(error, error_size, "write failed");
        return 0;
    }
    if (strcmp(value, "wrong") == 0)
        snprintf(error, error_size, "TLS redirect rejected");
    return 1;
}

int main(int argc, char **argv)
{
    char directory_template[] = "/tmp/cbs-fetch-test-XXXXXX";
    char *directory;
    FILE *recipe;
    char *source_text;
    long source_length;
    CbsTokenList tokens = {0};
    CbsNode *document;
    CbsSourceSet set = {0};
    CbsSourceSet broken = {0};
    CbsFetchService service;
    FetchState state = {0};
    const char *broken_url = "https://bad.invalid/source";
    const char *broken_digest = "3608bca1e44ea6c4d268eb6db02260269892c0b42b86bbf1e77a6fa16c3c9282";
    int ok = 1;

    if (argc != 2)
        return 2;
    directory = mkdtemp(directory_template);
    if (directory == NULL)
        return 1;
    recipe = fopen(argv[1], "rb");
    if (recipe == NULL || fseek(recipe, 0, SEEK_END) != 0)
        return 1;
    source_length = ftell(recipe);
    if (source_length < 0 || fseek(recipe, 0, SEEK_SET) != 0)
        return 1;
    source_text = cbs_allocate((size_t)source_length + 1);
    if (fread(source_text, 1, (size_t)source_length, recipe) !=
        (size_t)source_length) return 1;
    fclose(recipe);
    source_text[source_length] = '\0';
    if (!cbs_lex(argv[1], source_text, (size_t)source_length, &tokens)) ok = 0;
    document = cbs_parse(argv[1], source_text, (size_t)source_length, &tokens);
    if (document == NULL || !cbs_validate(document, argv[1], source_text) ||
        !cbs_sources_from_document(document, &set)) ok = 0;
    service.fetch = fetch_mock;
    service.user = &state;
    if (ok && !cbs_sources_fetch(&set, directory, &service, argv[1],
                                 source_text, document->location)) ok = 0;
    if (ok && state.calls != 2) ok = 0;
    if (ok && !cbs_sources_apply_execution_context(&set, &(CbsExecutionContext){0})) ok = 0;
    if (ok && !cbs_sources_fetch(&set, directory, NULL, argv[1], source_text,
                                 document->location)) ok = 0;
    if (ok && state.calls != 2) ok = 0;

    broken.items = cbs_allocate(sizeof(*broken.items));
    broken.count = 1;
    broken.items[0].kind = "main";
    broken.items[0].name = "broken";
    broken.items[0].urls = &broken_url;
    broken.items[0].url_count = 1;
    broken.items[0].sha256 = broken_digest;
    if (ok && cbs_sources_fetch(&broken, directory, &service, argv[1],
                                NULL, document->location)) ok = 0;
    free(broken.items);
    cbs_source_set_destroy(&set);
    cbs_node_destroy(document);
    cbs_token_list_destroy(&tokens);
    free(source_text);
    if (!ok) {
        fputs("source fetch tests: FAIL\n", stderr);
        return 1;
    }
    puts("source fetch tests: PASS (verified cache and diagnostic cause)");
    return 0;
}
