#define _POSIX_C_SOURCE 200809L

#include "cbs.h"

#include <archive.h>
#include <archive_entry.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char *read_all(const char *path, size_t *length)
{
    FILE *file = fopen(path, "rb");
    long size;
    char *text;

    if (file == NULL || fseek(file, 0, SEEK_END) != 0 ||
        (size = ftell(file)) < 0 || fseek(file, 0, SEEK_SET) != 0)
        return NULL;
    text = cbs_allocate((size_t)size + 1);
    if (fread(text, 1, (size_t)size, file) != (size_t)size) {
        fclose(file);
        free(text);
        return NULL;
    }
    fclose(file);
    text[size] = '\0';
    *length = (size_t)size;
    return text;
}

static int make_archive(const char *path)
{
    struct archive *archive = archive_write_new();
    struct archive_entry *entry;
    const char content[] = "support data\n";
    int ok = 0;

    if (archive == NULL || archive_write_set_format_pax_restricted(archive) != ARCHIVE_OK ||
        archive_write_open_filename(archive, path) != ARCHIVE_OK)
        goto done;
    entry = archive_entry_new();
    archive_entry_set_pathname(entry, "support-upstream");
    archive_entry_set_filetype(entry, AE_IFDIR);
    archive_entry_set_perm(entry, 0755);
    if (archive_write_header(archive, entry) != ARCHIVE_OK)
        goto done_entry;
    archive_entry_free(entry);
    entry = archive_entry_new();
    archive_entry_set_pathname(entry, "support-upstream/data.txt");
    archive_entry_set_filetype(entry, AE_IFREG);
    archive_entry_set_perm(entry, 0644);
    archive_entry_set_size(entry, sizeof(content) - 1);
    if (archive_write_header(archive, entry) != ARCHIVE_OK ||
        archive_write_data(archive, content, sizeof(content) - 1) !=
            (ssize_t)(sizeof(content) - 1))
        goto done_entry;
    ok = 1;
done_entry:
    archive_entry_free(entry);
done:
    if (archive != NULL) {
        archive_write_close(archive);
        archive_write_free(archive);
    }
    return ok;
}

int main(int argc, char **argv)
{
    char root[] = "/tmp/cbs-extract-XXXXXX";
    char archive_path[512], config_path[512], src[512], build[512], dest[512], result[512];
    char *source, *data;
    size_t length;
    CbsTokenList tokens = {0};
    CbsNode *document;
    CbsNode *prepare = NULL;
    CbsExecutionContext context;
    CbsNamedSource named[2];
    struct stat status;
    size_t index;
    int ok = 0;

    if (argc != 2 || mkdtemp(root) == NULL ||
        snprintf(archive_path, sizeof(archive_path), "%s/support.tar", root) >=
            (int)sizeof(archive_path) ||
        snprintf(src, sizeof(src), "%s/src", root) >= (int)sizeof(src) ||
        snprintf(build, sizeof(build), "%s/build", root) >= (int)sizeof(build) ||
        snprintf(dest, sizeof(dest), "%s/dest", root) >= (int)sizeof(dest) ||
        mkdir(src, 0700) != 0 || mkdir(build, 0700) != 0 ||
        mkdir(dest, 0700) != 0 || !make_archive(archive_path) ||
        snprintf(config_path, sizeof(config_path), "%s/config", root) >=
            (int)sizeof(config_path))
        return 1;
    {
        FILE *config = fopen(config_path, "wb");
        if (config == NULL || fputs("CONFIG_TEST=y\n", config) < 0 ||
            fclose(config) != 0)
            return 1;
    }
    source = read_all(argv[1], &length);
    document = source == NULL || !cbs_lex(argv[1], source, length, &tokens) ?
               NULL : cbs_parse(argv[1], source, length, &tokens);
    if (document == NULL || !cbs_validate(document, argv[1], source))
        goto done;
    for (index = 0; index < document->children[0]->child_count; ++index) {
        CbsNode *node = document->children[0]->children[index];
        if (node->kind == CBS_NODE_PHASE && strcmp(node->name, "prepare") == 0)
            prepare = node;
    }
    named[0].name = "support";
    named[0].path = archive_path;
    named[1].name = "config";
    named[1].path = config_path;
    memset(&context, 0, sizeof(context));
    context.recipe_path = argv[1];
    context.recipe_source = source;
    context.name = "extract-test";
    context.version = "1";
    context.release = 1;
    context.arch = "x86_64";
    context.src = src;
    context.build = build;
    context.dest = dest;
    context.working_directory = src;
    context.sources = named;
    context.source_count = 2;
    if (prepare == NULL || !cbs_execute_block(prepare, &context))
        goto done;
    snprintf(result, sizeof(result), "%s/support/data.txt", src);
    data = read_all(result, &length);
    if (data == NULL || strcmp(data, "support data\n") != 0 ||
        lstat(result, &status) != 0 || !S_ISREG(status.st_mode))
        goto done;
    free(data);
    snprintf(result, sizeof(result), "%s/.config", build);
    data = read_all(result, &length);
    if (data == NULL || strcmp(data, "CONFIG_TEST=y\n") != 0)
        goto done;
    free(data);
    if (lstat("/dev/null", &status) != 0)
        goto done;
    ok = 1;
done:
    free(source);
    if (document != NULL)
        cbs_node_destroy(document);
    cbs_token_list_destroy(&tokens);
    if (!ok) {
        fputs("extract execution test: FAIL\n", stderr);
        return 1;
    }
    puts("extract execution test: PASS (named source, confinement, and rename)");
    return 0;
}
