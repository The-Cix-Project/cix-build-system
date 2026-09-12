/* High-level build-to-manifest-to-CIXPKG package pipelines. */
#include "cbs.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zstd.h>

/* Select the compiler dependency that applies to the build pipeline. */
static const char *declared_compiler(const CbsNode *document) {
    const CbsNode *package;
    size_t i, j, k;
    if (document == NULL || document->child_count != 1)
        return NULL;
    package = document->children[0];
    for (i = 0; i < package->child_count; ++i) {
        const CbsNode *requires = package->children[i];
        if (requires->kind != CBS_NODE_REQUIRES)
            continue;
        for (j = 0; j < requires->child_count; ++j) {
            const CbsNode *group = requires->children[j];
            for (k = 0; k < group->child_count; ++k) {
                const CbsNode *dependency = group->children[k];
                if (strcmp(dependency->name, "compiler") == 0)
                    return dependency->value;
            }
        }
    }
    return NULL;
}

/* Return the artifact format selected by the immutable recipe revision. */
static const char *declared_format(const CbsNode *document) {
    const CbsNode *package;
    size_t index;
    if (document == NULL || document->child_count != 1)
        return NULL;
    package = document->children[0];
    for (index = 0; index < package->child_count; ++index)
        if (package->children[index]->kind == CBS_NODE_FORMAT)
            return package->children[index]->value;
    return NULL;
}
/* Compress a standalone file using the CIXPKG zstd settings. */
int cbs_cixpkg_compress(const char *input, const char *output) {
    FILE *in = fopen(input, "rb"), *out;
    long n;
    void *src, *dst;
    size_t bound, written;
    if (!in || fseek(in, 0, SEEK_END) || (n = ftell(in)) < 0 ||
        fseek(in, 0, SEEK_SET)) {
        if (in)
            fclose(in);
        return 0;
    }
    src = malloc((size_t)n);
    if (!src || fread(src, 1, (size_t)n, in) != (size_t)n) {
        free(src);
        fclose(in);
        return 0;
    }
    fclose(in);
    bound = ZSTD_compressBound((size_t)n);
    dst = malloc(bound);
    if (!dst) {
        free(src);
        return 0;
    }
    written = ZSTD_compress(dst, bound, src, (size_t)n, 19);
    free(src);
    if (ZSTD_isError(written)) {
        free(dst);
        return 0;
    }
    out = fopen(output, "wb");
    if (!out || fwrite(dst, 1, written, out) != written || fclose(out) != 0) {
        if (out)
            fclose(out);
        free(dst);
        return 0;
    }
    free(dst);
    return 1;
}

/* Decompress one bounded zstd frame into a file. */
int cbs_cixpkg_decompress(const char *input, const char *output) {
    FILE *in = fopen(input, "rb"), *out;
    long n;
    void *src, *dst;
    unsigned long long size;
    size_t written;
    if (!in || fseek(in, 0, SEEK_END) || (n = ftell(in)) < 0 ||
        fseek(in, 0, SEEK_SET)) {
        if (in)
            fclose(in);
        return 0;
    }
    src = malloc((size_t)n);
    if (!src || fread(src, 1, (size_t)n, in) != (size_t)n) {
        free(src);
        if (in)
            fclose(in);
        return 0;
    }
    fclose(in);
    size = ZSTD_getFrameContentSize(src, (size_t)n);
    if (size == ZSTD_CONTENTSIZE_ERROR || size == ZSTD_CONTENTSIZE_UNKNOWN ||
        size > 1024ULL * 1024ULL * 1024ULL) {
        free(src);
        return 0;
    }
    dst = malloc((size_t)size + 1);
    if (!dst) {
        free(src);
        return 0;
    }
    written = ZSTD_decompress(dst, (size_t)size, src, (size_t)n);
    free(src);
    if (ZSTD_isError(written) || written != (size_t)size) {
        free(dst);
        return 0;
    }
    out = fopen(output, "wb");
    if (!out || fwrite(dst, 1, written, out) != written || fclose(out) != 0) {
        if (out)
            fclose(out);
        free(dst);
        return 0;
    }
    free(dst);
    return 1;
}

/* Apply a mode and atomically rename one staged file. */
int cbs_install_atomic(const char *staged, const char *destination,
                       unsigned mode) {
    if (staged == NULL || destination == NULL || chmod(staged, mode) != 0)
        return 0;
    return rename(staged, destination) == 0;
}
/* Compare two files byte by byte without loading either whole file. */
int cbs_compare_files(const char *left, const char *right) {
    FILE *a = fopen(left, "rb"), *b = fopen(right, "rb");
    int x, y;
    if (!a || !b) {
        if (a)
            fclose(a);
        if (b)
            fclose(b);
        return 0;
    }
    do {
        x = fgetc(a);
        y = fgetc(b);
        if (x != y) {
            fclose(a);
            fclose(b);
            return 0;
        }
    } while (x != EOF);
    fclose(a);
    fclose(b);
    return 1;
}

/* Validate a recipe and package an already staged tree. */
int cbs_build_package(const char *recipe, const char *staged_root,
                      const char *package_path) {
    FILE *f;
    long n;
    char *text;
    size_t length;
    CbsTokenList tokens = {0};
    CbsNode *document;
    char manifest[4096];
    int ok;
    if (!recipe || !staged_root || !package_path)
        return 0;
    f = fopen(recipe, "rb");
    if (!f || fseek(f, 0, SEEK_END) || (n = ftell(f)) < 0 ||
        fseek(f, 0, SEEK_SET)) {
        if (f)
            fclose(f);
        return 0;
    }
    text = malloc((size_t)n + 1);
    if (!text || fread(text, 1, (size_t)n, f) != (size_t)n) {
        free(text);
        fclose(f);
        return 0;
    }
    fclose(f);
    text[n] = '\0';
    length = (size_t)n;
    ok = cbs_lex(recipe, text, length, &tokens);
    document = ok ? cbs_parse(recipe, text, length, &tokens) : NULL;
    ok = document != NULL && cbs_validate(document, recipe, text) &&
         strcmp(declared_format(document), "cixpkg") == 0;
    snprintf(manifest, sizeof(manifest), "%s/.cbs-manifest", staged_root);
    if (ok)
        ok = cbs_manifest_write(staged_root, manifest);
    if (ok)
        ok = cbs_cixpkg_write_tree(manifest, staged_root, package_path, "cbs");
    unlink(manifest);
    if (document)
        cbs_node_destroy(document);
    cbs_token_list_destroy(&tokens);
    free(text);
    return ok;
}

/* Execute a recipe, apply policy, and write its standalone artifact. */
int cbs_build_standalone_with_events(
    const char *recipe, const char *workspace, const char *package_path,
    const char *architecture, const CbsFetchService *fetch_service,
    const char *cache_directory, CbsFinalizePolicy finalize, void *user,
    CbsBuildEventSink event_sink, void *event_sink_user) {
    FILE *f;
    long n;
    char *text;
    CbsTokenList tokens = {0};
    CbsNode *document;
    CbsSourceSet sources = {0};
    CbsBuildPlan plan;
    CbsPackageIdentity identity;
    CbsExecutionContext context;
    char src[4096], build[4096], dest[4096], cache[4096], manifest[4096],
        *package_identity = NULL;
    unsigned flags = 0;
    int ok;
    if (!recipe || !workspace || !architecture)
        return 0;
    f = fopen(recipe, "rb");
    if (!f || fseek(f, 0, SEEK_END) || (n = ftell(f)) < 0 ||
        fseek(f, 0, SEEK_SET)) {
        if (f)
            fclose(f);
        return 0;
    }
    text = malloc((size_t)n + 1);
    if (!text || fread(text, 1, (size_t)n, f) != (size_t)n) {
        free(text);
        if (f)
            fclose(f);
        return 0;
    }
    fclose(f);
    text[n] = '\0';
    ok = cbs_lex(recipe, text, (size_t)n, &tokens);
    document = ok ? cbs_parse(recipe, text, (size_t)n, &tokens) : NULL;
    ok = document != NULL && cbs_validate(document, recipe, text) &&
         strcmp(declared_format(document), "cixpkg") == 0 &&
         cbs_build_plan(document, &plan) && cbs_workspace_prepare(workspace) &&
         cbs_sources_from_document(document, &sources);
    snprintf(src, sizeof(src), "%s/src", workspace);
    snprintf(build, sizeof(build), "%s/build", workspace);
    snprintf(dest, sizeof(dest), "%s/dest", workspace);
    snprintf(cache, sizeof(cache), "%s/cache", workspace);
    if (cache_directory != NULL)
        snprintf(cache, sizeof(cache), "%s", cache_directory);
    if (ok && sources.count > 0)
        ok = cbs_prepare_sources(&sources, cache, src, fetch_service, recipe,
                                 text, document->location);
    memset(&context, 0, sizeof(context));
    if (ok) {
        if (!cbs_identity_from_document(document, architecture, &identity))
            ok = 0;
        else {
            package_identity = cbs_identity_string(&identity);
            context.recipe_path = recipe;
            context.recipe_source = text;
            context.name = identity.name;
            context.version = identity.version;
            context.release = identity.release;
            context.arch = identity.architecture;
            context.compiler = declared_compiler(document);
            context.src = src;
            context.build = build;
            context.dest = dest;
            context.jobs = 1;
            context.working_directory = build;
            context.event_sink = event_sink;
            context.event_sink_user = event_sink_user;
            ok = cbs_sources_apply_execution_context(&sources, &context) &&
                 cbs_emit_build_event(&context, "build-begin", NULL, NULL,
                                      NULL, 0, 0, 0, 0) &&
                 cbs_execute_plan(&plan, &context);
        }
    }
    if (ok && finalize != NULL) {
        ok = finalize(dest, user);
        if (ok)
            flags = 1;
    }
    if (ok && package_path != NULL) {
        snprintf(manifest, sizeof(manifest), "%s/.cbs-manifest", dest);
        ok = cbs_manifest_write(dest, manifest) &&
             cbs_cixpkg_write_tree_with_flags(manifest, dest, package_path,
                                              package_identity, flags);
        unlink(manifest);
    }
    if (context.event_sink != NULL && context.name != NULL)
        cbs_emit_build_event(&context, "build-end", NULL, NULL,
                             ok ? NULL : "build failed", ok ? 0 : 1, 0, 0,
                             0);
    free(package_identity);
    cbs_source_set_destroy(&sources);
    if (document)
        cbs_node_destroy(document);
    cbs_token_list_destroy(&tokens);
    free(text);
    return ok;
}
int cbs_build_standalone_with_cache_policy(
    const char *recipe, const char *workspace, const char *package_path,
    const char *architecture, const CbsFetchService *fetch_service,
    const char *cache_directory, CbsFinalizePolicy finalize, void *user) {
    return cbs_build_standalone_with_events(
        recipe, workspace, package_path, architecture, fetch_service,
        cache_directory, finalize, user, NULL, NULL);
}
/* Use the standalone pipeline without a finalization callback. */
int cbs_build_standalone_with_cache(const char *recipe, const char *workspace,
                                    const char *package_path,
                                    const char *architecture,
                                    const CbsFetchService *fetch_service,
                                    const char *cache_directory) {
    return cbs_build_standalone_with_cache_policy(
        recipe, workspace, package_path, architecture, fetch_service,
        cache_directory, NULL, NULL);
}
/* Compatibility wrapper for the default source-cache pipeline. */
int cbs_build_standalone(const char *recipe, const char *workspace,
                         const char *package_path, const char *architecture,
                         const CbsFetchService *fetch_service) {
    return cbs_build_standalone_with_cache(recipe, workspace, package_path,
                                           architecture, fetch_service, NULL);
}
