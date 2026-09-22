/* High-level build-to-manifest-to-CIXPKG package pipelines. */
#include "cbs.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
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

static const CbsNode *declared_tools(const CbsNode *document) {
    const CbsNode *package;
    size_t index;
    if (document == NULL || document->child_count != 1)
        return NULL;
    package = document->children[0];
    for (index = 0; index < package->child_count; ++index)
        if (package->children[index]->kind == CBS_NODE_TOOLS)
            return package->children[index];
    return NULL;
}

/* Resolve and publish CBS-owned tool entries before any phase runs. */
static int materialize_tools(const CbsNode *tools,
                             CbsExecutionContext *context, char *path,
                             size_t path_size) {
    char tool_directory[4096];
    char **targets = NULL;
    struct stat status;
    size_t index;
    int published = 0;
    int result = 0;
    if (tools == NULL)
        return 1;
    if (context->build == NULL || stat(context->build, &status) != 0 ||
        !S_ISDIR(status.st_mode) ||
        snprintf(path, path_size, "%s/.cbs-tools", context->build) >=
            (int)path_size)
        return 0;
    if (snprintf(tool_directory, sizeof(tool_directory), "%s", path) >=
        (int)sizeof(tool_directory))
        return 0;
    targets = calloc(tools->child_count, sizeof(*targets));
    if (targets == NULL)
        return 0;

    /* Resolve every target before creating the published directory.  A
     * failed declaration must not leave a partially usable tool namespace. */
    for (index = 0; index < tools->child_count; ++index) {
        const CbsNode *tool = tools->children[index];
        const char *target_name = context->compiler;
        if (tool->kind != CBS_NODE_TOOL || tool->value == NULL ||
            strcmp(tool->name, "compiler") != 0 || tool->second_flag != 1 ||
            target_name == NULL)
            goto done;
        if (!cbs_command_path_is_valid(context->command_path))
            goto done;
        targets[index] = cbs_resolve_executable(
            target_name, context->working_directory, context->command_path);
        if (targets[index] == NULL)
            goto done;
    }
    if (mkdir(path, 0755) != 0 && errno != EEXIST)
        goto done;
    for (index = 0; index < tools->child_count; ++index) {
        const CbsNode *tool = tools->children[index];
        char link_path[4096];
        if (snprintf(link_path, sizeof(link_path), "%s/%s", path,
                     tool->value) >= (int)sizeof(link_path))
            goto done;
        unlink(link_path);
        if (symlink(targets[index], link_path) != 0)
            goto done;
        published = 1;
    }
    if (published && snprintf(path + strlen(path), path_size - strlen(path),
                              ":%s", context->command_path) >=
                         (int)(path_size - strlen(path)))
        goto done;
    context->tool_directory = cbs_duplicate(tool_directory);
    if (result == 0 && context->tool_directory != NULL)
        context->tool_target = cbs_duplicate(targets[0]);
    result = context->tool_directory != NULL &&
             (context->tool_target != NULL || tools->child_count == 0);
done:
    if (targets != NULL) {
        for (index = 0; index < tools->child_count; ++index)
            free(targets[index]);
        free(targets);
    }
    return result;
}

/* Append provenance facts to the canonical manifest before it is packaged. */
static int append_provenance(const char *manifest, const char *recipe,
                             const char *recipe_text, const CbsSourceSet *sources,
                             const CbsExecutionContext *context) {
    FILE *file;
    char digest[65];
    size_t index;

    if (!cbs_digest_text(recipe_text, strlen(recipe_text), digest))
        return 0;
    file = fopen(manifest, "ab");
    if (file == NULL)
        return 0;
    if (fprintf(file, "m recipe %s\n", recipe) < 0 ||
        fprintf(file, "m recipe_sha256 %s\n", digest) < 0 ||
        fprintf(file, "m cbs_version %s\n", CBS_VERSION) < 0 ||
        fprintf(file, "m architecture %s\n", context->arch) < 0 ||
        fprintf(file, "m toolchain %s\n", context->compiler == NULL
                                               ? "none"
                                               : context->compiler) < 0)
        goto failure;
    if (context->tool_policy != NULL) {
        if (context->tool_target != NULL &&
            fprintf(file, "m tool_policy_target compiler %s\n",
                    context->tool_target) < 0)
            goto failure;
        for (index = 0; index < context->tool_policy->child_count; ++index) {
            const CbsNode *tool = context->tool_policy->children[index];
            if (fprintf(file, "m tool_policy %s alias %s\n", tool->name,
                        tool->value) < 0)
                goto failure;
        }
    }
    for (index = 0; index < sources->count; ++index) {
        const CbsSource *source = &sources->items[index];
        if (source->url_count == 0 ||
            fprintf(file, "m source %s %s %s\n", source->name,
                    source->urls[0], source->sha256) < 0)
            goto failure;
    }
    return fclose(file) == 0;
failure:
    fclose(file);
    return 0;
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

static const char *declared_license(const CbsNode *document) {
    const CbsNode *package;
    size_t index;
    if (document == NULL || document->child_count != 1)
        return NULL;
    package = document->children[0];
    for (index = 0; index < package->child_count; ++index)
        if (package->children[index]->kind == CBS_NODE_LICENSE)
            return package->children[index]->value;
    return NULL;
}

/* Report a high-level pipeline rejection that has no parser diagnostic. */
static void pipeline_error(const char *recipe, const char *source,
                           CbsLocation location, const char *step,
                           const char *detail) {
    char message[768];
    snprintf(message, sizeof(message), "%s: %s", step, detail);
    cbs_diagnostic(recipe, source, location, "error", "CPDL-E4001",
                   CBS_DIAG_RUNTIME, message);
}

static int node_uses_firmware(const CbsNode *node) {
    size_t index;
    if ((node->value != NULL &&
         (strstr(node->value, "${firmware}") != NULL ||
          strcmp(node->value, "$firmware") == 0)) ||
        (node->name != NULL && strstr(node->name, "${firmware}") != NULL) ||
        (node->second_value != NULL &&
         strstr(node->second_value, "${firmware}") != NULL))
        return 1;
    for (index = 0; index < node->child_count; ++index)
        if (node_uses_firmware(node->children[index]))
            return 1;
    return 0;
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
        ok = cbs_manifest_write_with_license(staged_root, manifest,
                                              declared_license(document));
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
int cbs_build_standalone_with_events_policy_path(
    const char *recipe, const char *workspace, const char *package_path,
    const char *architecture, const CbsFetchService *fetch_service,
    const char *cache_directory, CbsFinalizePolicy finalize, void *user,
    const char *firmware_root, const CbsPrunePolicy *prune_policy,
    const char *command_path,
    const char *library_path,
    CbsBuildEventSink event_sink,
    void *event_sink_user) {
    FILE *f;
    long n;
    char *text;
    CbsTokenList tokens = {0};
    CbsNode *document;
    CbsSourceSet sources = {0};
    CbsBuildPlan plan;
    CbsPackageIdentity identity;
    CbsExecutionContext context;
    char build_id[64];
    CbsManifestEntry *entries = NULL;
    size_t entry_count = 0;
    char src[4096], build[4096], dest[4096], cache[4096], manifest[4096],
        manifest_error[512], *package_identity = NULL;
    char tool_command_path[8192];
    unsigned flags = 0;
    int ok;
    if (!recipe || !workspace || !architecture ||
        (command_path != NULL && !cbs_command_path_is_valid(command_path)) ||
        (library_path != NULL && !cbs_library_path_is_valid(library_path)))
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
    ok = document != NULL && cbs_validate(document, recipe, text);
    if (ok && firmware_root == NULL && node_uses_firmware(document)) {
        pipeline_error(recipe, text, document->location, "firmware root",
                       "recipe uses ${firmware}, but no firmware root was supplied");
        ok = 0;
    }
    if (ok && strcmp(declared_format(document), "cixpkg") != 0) {
        pipeline_error(recipe, text, document->location, "format",
                       "standalone builds require cixpkg");
        ok = 0;
    }
    if (ok && !cbs_build_plan(document, &plan)) {
        pipeline_error(recipe, text, document->location, "build plan",
                       "recipe has no executable phase plan");
        ok = 0;
    }
    if (ok && !cbs_workspace_prepare(workspace)) {
        char detail[512];
        snprintf(detail, sizeof(detail), "cannot prepare %s: %s", workspace,
                 strerror(errno));
        pipeline_error(recipe, text, document->location, "workspace", detail);
        ok = 0;
    }
    if (ok && !cbs_sources_from_document(document, &sources)) {
        pipeline_error(recipe, text, document->location, "sources",
                       "cannot construct source set");
        ok = 0;
    }
    snprintf(src, sizeof(src), "%s/src", workspace);
    snprintf(build, sizeof(build), "%s/build", workspace);
    snprintf(dest, sizeof(dest), "%s/dest", workspace);
    snprintf(cache, sizeof(cache), "%s/cache", workspace);
    if (cache_directory != NULL)
        snprintf(cache, sizeof(cache), "%s", cache_directory);
    memset(&context, 0, sizeof(context));
    snprintf(build_id, sizeof(build_id), "%ld-%ld", (long)time(NULL),
             (long)getpid());
    context.recipe_path = recipe;
    context.recipe_source = text;
    context.event_sink = event_sink;
    context.event_sink_user = event_sink_user;
    context.build_id = build_id;
    context.log_directory = getenv("CBS_LOG_DIR");
    if (ok && sources.count > 0)
        ok = cbs_prepare_sources_with_events(
            &sources, cache, src, fetch_service, recipe, text,
            document->location, &context);
    if (ok) {
        if (!cbs_identity_from_document(document, architecture, &identity)) {
            pipeline_error(recipe, text, document->location, "identity",
                           "cannot derive package identity");
            ok = 0;
        } else {
            package_identity = cbs_identity_string(&identity);
            context.name = identity.name;
            context.version = identity.version;
            context.release = identity.release;
            context.arch = identity.architecture;
            context.compiler = declared_compiler(document);
            context.src = src;
            context.build = build;
            context.dest = dest;
            context.firmware_root = firmware_root;
            context.jobs = 1;
            context.working_directory = build;
            context.command_path = command_path == NULL
                                       ? CBS_DEFAULT_COMMAND_PATH
                                       : command_path;
            context.library_path = library_path == NULL
                                       ? CBS_DEFAULT_LIBRARY_PATH
                                       : library_path;
            context.tool_policy = declared_tools(document);
            if (!materialize_tools(context.tool_policy, &context,
                                   tool_command_path,
                                   sizeof(tool_command_path))) {
                pipeline_error(recipe, text, document->location, "tools",
                               "cannot resolve or materialize tool policy");
                ok = 0;
            } else if (context.tool_policy != NULL &&
                       context.tool_directory != NULL) {
                context.command_path = tool_command_path;
            }
            if (ok)
                ok = cbs_sources_apply_execution_context(&sources, &context);
            if (!ok)
                pipeline_error(recipe, text, document->location, "sources",
                               "cannot apply verified source bindings");
            if (ok && !cbs_emit_build_event(&context, "build-begin", NULL,
                                            NULL, NULL, 0, 0, 0, 0))
                ok = 0;
            if (ok && !cbs_execute_plan(&plan, &context))
                ok = 0;
        }
    }
    if (ok && finalize != NULL) {
        ok = finalize(dest, user);
        if (ok)
            flags = 1;
        else
            pipeline_error(recipe, text, document->location, "finalize",
                           "finalization policy rejected the staged tree");
    }
    if (ok && prune_policy != NULL &&
        !cbs_prune_staged_tree(dest, prune_policy, &context)) {
        pipeline_error(recipe, text, document->location, "prune",
                       "prune policy rejected the staged tree");
        ok = 0;
    }
    if (ok && package_path != NULL) {
        snprintf(manifest, sizeof(manifest), "%s/.cbs-manifest", dest);
        ok = cbs_manifest_write_with_license_error(
            dest, manifest, declared_license(document), manifest_error,
            sizeof(manifest_error));
        if (!ok)
            pipeline_error(recipe, text, document->location, "manifest",
                           manifest_error[0] != '\0'
                               ? manifest_error
                               : "cannot write staged-tree manifest");
        if (ok && !append_provenance(manifest, recipe, text, &sources,
                                     &context)) {
            pipeline_error(recipe, text, document->location, "provenance",
                           "cannot append build provenance");
            ok = 0;
        }
        if (ok) {
            ok = cbs_manifest_collect_with_error(
                dest, &entries, &entry_count, manifest_error,
                sizeof(manifest_error));
            if (!ok)
                pipeline_error(recipe, text, document->location, "manifest",
                               manifest_error[0] != '\0'
                                   ? manifest_error
                                   : "cannot collect staged-tree entries");
        }
        if (ok) {
            size_t index;
            context.current_tree_files = (unsigned long long)entry_count;
            context.current_tree_bytes = 0;
            for (index = 0; index < entry_count; ++index)
                context.current_tree_bytes += entries[index].size;
        }
        if (ok)
            ok = cbs_cixpkg_write_tree_with_flags(
                manifest, dest, package_path, package_identity, flags);
        if (!ok && entries != NULL)
            pipeline_error(recipe, text, document->location, "package output",
                           "cannot write CIXPKG output");
        if (ok) {
            struct stat artifact;
            if (stat(package_path, &artifact) != 0) {
                pipeline_error(recipe, text, document->location,
                               "package output", "cannot stat CIXPKG output");
                ok = 0;
            } else
                context.current_artifact_bytes =
                    (unsigned long long)artifact.st_size;
        }
        unlink(manifest);
        if (ok && !cbs_emit_build_event(&context, "artifact-finalized", NULL,
                                        package_path, package_identity, 0, 0,
                                        0, 0))
            ok = 0;
    }
    if (context.event_sink != NULL && context.name != NULL)
        cbs_emit_build_event(&context, "build-end", NULL, NULL,
                             ok ? NULL : "build failed", ok ? 0 : 1, 0, 0,
                             0);
    free(package_identity);
    cbs_manifest_entries_destroy(entries, entry_count);
    cbs_source_set_destroy(&sources);
    if (document)
        cbs_node_destroy(document);
    cbs_token_list_destroy(&tokens);
    free(text);
    return ok;
}

int cbs_build_standalone_with_events_policy(
    const char *recipe, const char *workspace, const char *package_path,
    const char *architecture, const CbsFetchService *fetch_service,
    const char *cache_directory, CbsFinalizePolicy finalize, void *user,
    const char *firmware_root, const CbsPrunePolicy *prune_policy,
    CbsBuildEventSink event_sink, void *event_sink_user) {
    return cbs_build_standalone_with_events_policy_path(
        recipe, workspace, package_path, architecture, fetch_service,
        cache_directory, finalize, user, firmware_root, prune_policy, NULL,
        NULL,
        event_sink, event_sink_user);
}

int cbs_build_standalone_with_events(
    const char *recipe, const char *workspace, const char *package_path,
    const char *architecture, const CbsFetchService *fetch_service,
    const char *cache_directory, CbsFinalizePolicy finalize, void *user,
    CbsBuildEventSink event_sink, void *event_sink_user) {
    return cbs_build_standalone_with_events_policy(
        recipe, workspace, package_path, architecture, fetch_service,
        cache_directory, finalize, user, NULL, NULL, event_sink,
        event_sink_user);
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
