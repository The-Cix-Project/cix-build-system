/* High-level build-to-manifest-to-CIXPKG package pipelines. */
#include "cbs.h"
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <zstd.h>

static int valid_input_binding(const CbsInputBinding *inputs,
                               size_t input_count) {
    size_t index;
    size_t character;
    for (index = 0; index < input_count; ++index) {
        const char *name = inputs[index].name;
        const char *path = inputs[index].path;
        if (name == NULL || path == NULL || path[0] != '/' ||
            name[0] == '\0' ||
            !((name[0] >= 'A' && name[0] <= 'Z') ||
              (name[0] >= 'a' && name[0] <= 'z') || name[0] == '_'))
            return 0;
        for (character = 1; name[character] != '\0'; ++character)
            if (!((name[character] >= 'A' && name[character] <= 'Z') ||
                  (name[character] >= 'a' && name[character] <= 'z') ||
                  (name[character] >= '0' && name[character] <= '9') ||
                  name[character] == '_'))
                return 0;
        for (character = 0; character < index; ++character)
            if (strcmp(inputs[character].name, name) == 0)
                return 0;
    }
    return 1;
}

static int valid_digest(const char *digest) {
    size_t index;
    if (digest == NULL || strlen(digest) != 64)
        return 0;
    for (index = 0; index < 64; ++index)
        if (!((digest[index] >= '0' && digest[index] <= '9') ||
              (digest[index] >= 'a' && digest[index] <= 'f') ||
              (digest[index] >= 'A' && digest[index] <= 'F')))
            return 0;
    return 1;
}

static int valid_tool_identities(const CbsToolIdentity *identities,
                                 size_t count) {
    size_t index, other;
    if (count != 0 && identities == NULL)
        return 0;
    for (index = 0; index < count; ++index) {
        if (identities[index].name == NULL || identities[index].version == NULL ||
            identities[index].name[0] == '\0' || identities[index].version[0] == '\0' ||
            !valid_digest(identities[index].manifest_digest))
            return 0;
        for (other = 0; other < index; ++other)
            if (strcmp(identities[other].name, identities[index].name) == 0 &&
                strcmp(identities[other].version, identities[index].version) == 0)
                return 0;
    }
    return 1;
}

static int fingerprint_append(unsigned char **buffer, size_t *length,
                              size_t *capacity, const char *name,
                              const char *value) {
    size_t name_length = strlen(name);
    size_t value_length = strlen(value == NULL ? "" : value);
    size_t needed = name_length + value_length + 2;
    unsigned char *grown;
    if (needed > SIZE_MAX - *length)
        return 0;
    needed += *length;
    if (needed > *capacity) {
        size_t next = *capacity == 0 ? 1024 : *capacity;
        while (next < needed) {
            if (next > SIZE_MAX / 2)
                return 0;
            next *= 2;
        }
        grown = realloc(*buffer, next);
        if (grown == NULL)
            return 0;
        *buffer = grown;
        *capacity = next;
    }
    memcpy(*buffer + *length, name, name_length);
    *length += name_length;
    (*buffer)[(*length)++] = '=';
    memcpy(*buffer + *length, value == NULL ? "" : value, value_length);
    *length += value_length;
    (*buffer)[(*length)++] = '\n';
    return 1;
}

static int tool_policy_contains(const CbsNode *tools, const char *value);

static int fingerprint_append_tools(unsigned char **buffer, size_t *length,
                                    size_t *capacity, const CbsNode *node,
                                    const char *command_path,
                                    const CbsNode *tool_policy,
                                    size_t *number) {
    size_t index;
    if (node == NULL)
        return 1;
    if (node->kind == CBS_NODE_RUN && node->value != NULL &&
        (tool_policy == NULL || !tool_policy_contains(tool_policy,
                                                      node->value))) {
        char field[128], digest[65];
        char *resolved;
        if (strchr(node->value, '$') != NULL ||
            (node->value[0] != '/' && strchr(node->value, '/') != NULL))
            return 0;
        resolved = node->value[0] == '/' ? cbs_duplicate(node->value) :
            cbs_resolve_executable(node->value, "/",
                                   command_path == NULL ? CBS_DEFAULT_COMMAND_PATH : command_path);
        if (resolved == NULL || !cbs_digest_file(resolved, digest)) {
            free(resolved);
            return 0;
        }
        if (snprintf(field, sizeof(field), "tool.%zu", (*number)++) >=
                (int)sizeof(field) ||
            !fingerprint_append(buffer, length, capacity, field, digest)) {
            free(resolved);
            return 0;
        }
        free(resolved);
    }
    for (index = 0; index < node->child_count; ++index)
        if (!fingerprint_append_tools(buffer, length, capacity,
                                      node->children[index], command_path,
                                      tool_policy, number))
            return 0;
    return 1;
}

/* Explain the first tool that prevents a stable fingerprint.  This is kept
 * separate from fingerprint_append_tools so packaging can report an
 * unavailable key without turning an otherwise valid build into a failure. */
static int fingerprint_unavailable_reason(const CbsNode *node, char *reason,
                                          size_t reason_size) {
    size_t index;
    if (node == NULL)
        return 0;
    if (node->kind == CBS_NODE_RUN && node->value != NULL &&
        (strchr(node->value, '$') != NULL ||
         (node->value[0] != '/' && strchr(node->value, '/') != NULL))) {
        if (snprintf(reason, reason_size, "dynamic-run:%s", node->value) >=
            (int)reason_size)
            return 0;
        return 1;
    }
    for (index = 0; index < node->child_count; ++index)
        if (fingerprint_unavailable_reason(node->children[index], reason,
                                           reason_size))
            return 1;
    return 0;
}

/* Add a canonical digest of a directory tree.  The root path is deliberately
 * excluded so temporary workspace names do not poison reproducibility.  Search
 * roots are complete material inputs: headers, archives, symlinks, and modes
 * matter just as much as executable and shared-library files. */
static int fingerprint_append_tree(unsigned char **buffer, size_t *length,
                                   size_t *capacity, const char *root,
                                   const char *label) {
    CbsManifestEntry *entries = NULL;
    size_t count = 0, index;
    if (root == NULL || !cbs_manifest_collect(root, &entries, &count))
        return 0;
    for (index = 0; index < count; ++index) {
        char field[4096], value[8192];
        const CbsManifestEntry *entry = &entries[index];
        if (snprintf(field, sizeof(field), "%s.%s", label, entry->path) >=
                (int)sizeof(field) ||
            snprintf(value, sizeof(value), "%c|%u|%llu|%s|%s", entry->type,
                     entry->mode, entry->size,
                     entry->digest == NULL ? "" : entry->digest,
                     entry->target == NULL ? "" : entry->target) >=
                (int)sizeof(value) ||
            !fingerprint_append(buffer, length, capacity, field, value)) {
            cbs_manifest_entries_destroy(entries, count);
            return 0;
        }
    }
    cbs_manifest_entries_destroy(entries, count);
    return 1;
}

static int fingerprint_append_search_paths(unsigned char **buffer,
                                           size_t *length, size_t *capacity,
                                           const char *paths, const char *label) {
    const char *cursor = paths;
    while (cursor != NULL && *cursor != '\0') {
        const char *end = strchr(cursor, ':');
        size_t size = end == NULL ? strlen(cursor) : (size_t)(end - cursor);
        char root[4096];
        if (size == 0 || size >= sizeof(root))
            return 0;
        memcpy(root, cursor, size);
        root[size] = '\0';
        {
            struct stat status;
            if (stat(root, &status) != 0) {
                char field[128];
                char value[4096];
                if (errno != ENOENT ||
                    snprintf(field, sizeof(field), "%s_root", label) >=
                        (int)sizeof(field) ||
                    snprintf(value, sizeof(value), "%s absent", root) >=
                        (int)sizeof(value) ||
                    !fingerprint_append(buffer, length, capacity, field, value)) {
                    return 0;
                }
                cursor = end == NULL ? NULL : end + 1;
                continue;
            }
            if (!S_ISDIR(status.st_mode)) {
                return 0;
            }
        }
        if (!fingerprint_append_tree(buffer, length, capacity, root, label)) {
            return 0;
        }
        cursor = end == NULL ? NULL : end + 1;
    }
    return 1;
}

static int fingerprint_append_tool_identities(
    unsigned char **buffer, size_t *length, size_t *capacity,
    const CbsToolIdentity *identities, size_t count) {
    size_t selected, index;
    const char *last_name = "";
    const char *last_version = "";
    for (selected = 0; selected < count; ++selected) {
        size_t best = SIZE_MAX;
        char field[4096];
        for (index = 0; index < count; ++index) {
            int after = strcmp(identities[index].name, last_name) > 0 ||
                        (strcmp(identities[index].name, last_name) == 0 &&
                         strcmp(identities[index].version, last_version) > 0);
            if (after &&
                (best == SIZE_MAX ||
                 strcmp(identities[index].name, identities[best].name) < 0 ||
                 (strcmp(identities[index].name, identities[best].name) == 0 &&
                  strcmp(identities[index].version, identities[best].version) < 0)))
                best = index;
        }
        if (best == SIZE_MAX ||
            snprintf(field, sizeof(field), "tool_identity.%s@%s",
                     identities[best].name, identities[best].version) >=
                (int)sizeof(field) ||
            !fingerprint_append(buffer, length, capacity, field,
                                identities[best].manifest_digest))
            return 0;
        last_name = identities[best].name;
        last_version = identities[best].version;
    }
    return 1;
}

static const char *declared_compiler(const CbsNode *document);
static const CbsNode *declared_tools(const CbsNode *document);

static int tool_policy_contains(const CbsNode *tools, const char *value) {
    size_t index;
    if (tools == NULL || value == NULL)
        return 0;
    for (index = 0; index < tools->child_count; ++index)
        if (tools->children[index]->kind == CBS_NODE_TOOL &&
            tools->children[index]->value != NULL &&
            strcmp(tools->children[index]->value, value) == 0)
            return 1;
    return 0;
}

/* Fingerprint the stable inputs to a declared tool namespace.  The published
 * wrapper directory is workspace state, so its resolved target identities are
 * recorded instead; this keeps cbs fingerprint and build equivalent. */
static int fingerprint_append_tool_policy(
    unsigned char **buffer, size_t *length, size_t *capacity,
    const CbsNode *document, const char *command_path) {
    const CbsNode *tools = declared_tools(document);
    const char *compiler;
    char *resolved;
    char digest[65];
    size_t index;
    if (tools == NULL)
        return 1;
    compiler = declared_compiler(document);
    if (compiler == NULL)
        return 0;
    resolved = cbs_resolve_executable(
        compiler, "/", command_path == NULL ? CBS_DEFAULT_COMMAND_PATH
                                               : command_path);
    if (resolved == NULL || !cbs_digest_file(resolved, digest)) {
        free(resolved);
        return 0;
    }
    for (index = 0; index < tools->child_count; ++index) {
        char field[4096];
        const CbsNode *tool = tools->children[index];
        if (tool->kind != CBS_NODE_TOOL || tool->value == NULL ||
            snprintf(field, sizeof(field), "tool_policy.%s", tool->value) >=
                (int)sizeof(field) ||
            !fingerprint_append(buffer, length, capacity, field, digest)) {
            free(resolved);
            return 0;
        }
    }
    free(resolved);
    return 1;
}

int cbs_build_fingerprint_with_context(
    const char *recipe, const char *architecture, const char *command_path,
    const char *library_path, const CbsInputBinding *inputs, size_t input_count,
    const CbsFingerprintContext *material_context, char output[65]) {
    FILE *file = NULL;
    long size;
    char *source = NULL;
    size_t length, input, normalized, selected;
    const char *last_input_name = "";
    CbsTokenList tokens = {0};
    CbsNode *document = NULL;
    unsigned char *fingerprint = NULL;
    size_t fingerprint_length = 0, fingerprint_capacity = 0;
    char digest[65];
    int ok = 0;

    if (recipe == NULL || architecture == NULL || output == NULL ||
        !cbs_command_path_is_valid(command_path == NULL
                                        ? CBS_DEFAULT_COMMAND_PATH
                                        : command_path) ||
        !cbs_library_path_is_valid(library_path == NULL
                                       ? CBS_DEFAULT_LIBRARY_PATH
                                       : library_path) ||
        (input_count != 0 && inputs == NULL) ||
        (inputs != NULL && !valid_input_binding(inputs, input_count)) ||
        (material_context != NULL && material_context->environment_count != 0 &&
         material_context->environment == NULL) ||
        (material_context != NULL &&
         !valid_tool_identities(material_context->tool_identities,
                                material_context->tool_identity_count)))
        return 0;
    file = fopen(recipe, "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0 ||
        (size = ftell(file)) < 0 || fseek(file, 0, SEEK_SET) != 0)
        goto done;
    source = malloc((size_t)size + 1);
    if (source == NULL || fread(source, 1, (size_t)size, file) != (size_t)size)
        goto done;
    source[size] = '\0';
    fclose(file);
    file = NULL;
    for (length = 0, normalized = 0; length < (size_t)size;) {
        if (source[length] == '\r' && length + 1 < (size_t)size &&
            source[length + 1] == '\n')
            ++length;
        source[normalized++] = source[length++];
    }
    source[normalized] = '\0';
    if (!cbs_lex(recipe, source, normalized, &tokens))
        goto done;
    document = cbs_parse(recipe, source, normalized, &tokens);
    if (document == NULL || !cbs_validate(document, recipe, source))
        goto done;
    if (!cbs_digest_text(source, normalized, digest) ||
        !fingerprint_append(&fingerprint, &fingerprint_length,
                            &fingerprint_capacity, "recipe_sha256", digest) ||
        !fingerprint_append(&fingerprint, &fingerprint_length,
                            &fingerprint_capacity, "cbs_version", CBS_VERSION) ||
        !fingerprint_append(&fingerprint, &fingerprint_length,
                            &fingerprint_capacity, "cpdl_version", "1") ||
        !fingerprint_append(&fingerprint, &fingerprint_length,
                            &fingerprint_capacity, "cpdl_contract",
                            CBS_CPDL_CONTRACT) ||
        !fingerprint_append(&fingerprint, &fingerprint_length,
                            &fingerprint_capacity, "cixpkg_version", CBS_CIXPKG_CONTRACT) ||
        !fingerprint_append(&fingerprint, &fingerprint_length,
                            &fingerprint_capacity, "architecture", architecture) ||
        !fingerprint_append(&fingerprint, &fingerprint_length,
                            &fingerprint_capacity, "command_path",
                            command_path == NULL ? CBS_DEFAULT_COMMAND_PATH
                                                 : command_path) ||
        !fingerprint_append(&fingerprint, &fingerprint_length,
                            &fingerprint_capacity, "library_path",
                            library_path == NULL ? CBS_DEFAULT_LIBRARY_PATH
                                                 : library_path))
        goto done;
    if (material_context != NULL) {
        const char *paths[3] = {material_context->firmware_root,
                                material_context->finalize_path, NULL};
        const char *names[] = {"firmware_root", "finalize_path",
                               "tool_directory"};
        size_t material_count = 3;
        size_t material;
        if (declared_tools(document) == NULL)
            paths[2] = material_context->tool_directory;
        for (material = 0; material < material_count; ++material) {
            char digest_field[96], material_digest[65];
            struct stat status;
            if (paths[material] == NULL)
                continue;
            if (stat(paths[material], &status) != 0)
                goto done;
            if (S_ISDIR(status.st_mode)) {
                if (!fingerprint_append_tree(
                        &fingerprint, &fingerprint_length, &fingerprint_capacity,
                        paths[material], names[material]))
                    goto done;
                continue;
            }
            if (!fingerprint_append(&fingerprint, &fingerprint_length,
                                    &fingerprint_capacity, names[material],
                                    paths[material]) ||
                !S_ISREG(status.st_mode) ||
                !cbs_digest_file(paths[material], material_digest))
                goto done;
            {
                snprintf(digest_field, sizeof(digest_field), "%s_sha256",
                         names[material]);
                if (!fingerprint_append(&fingerprint, &fingerprint_length,
                                        &fingerprint_capacity, digest_field,
                                        material_digest))
                    goto done;
            }
        }
        if (!fingerprint_append_tool_policy(
                &fingerprint, &fingerprint_length, &fingerprint_capacity,
                document, command_path)) {
            goto done;
        }
        if (material_context->tool_identity_count != 0) {
            if (!fingerprint_append_tool_identities(
                    &fingerprint, &fingerprint_length, &fingerprint_capacity,
                    material_context->tool_identities,
                    material_context->tool_identity_count))
                goto done;
        } else if (declared_tools(document) == NULL &&
                   (!fingerprint_append_search_paths(
                       &fingerprint, &fingerprint_length, &fingerprint_capacity,
                       command_path == NULL ? CBS_DEFAULT_COMMAND_PATH : command_path,
                       "command") ||
                   !fingerprint_append_search_paths(
                       &fingerprint, &fingerprint_length, &fingerprint_capacity,
                       library_path == NULL ? CBS_DEFAULT_LIBRARY_PATH : library_path,
                       "library")))
            goto done;
        if (declared_tools(document) == NULL ||
            material_context->tool_identity_count != 0) {
        if (!fingerprint_append(&fingerprint, &fingerprint_length,
                                &fingerprint_capacity, "prune.strip_debug",
                                material_context->prune_strip_debug ? "1" : "0") ||
            !fingerprint_append(&fingerprint, &fingerprint_length,
                                &fingerprint_capacity, "prune.drop_static_archives",
                                material_context->prune_drop_static_archives ? "1" : "0") ||
            !fingerprint_append(&fingerprint, &fingerprint_length,
                                &fingerprint_capacity, "prune.drop_libtool_archives",
                                material_context->prune_drop_libtool_archives ? "1" : "0"))
            goto done;
        {
            const char *last_environment = "";
            for (selected = 0;
                 selected < material_context->environment_count; ++selected) {
            size_t selected_input = SIZE_MAX;
            size_t candidate;
            for (candidate = 0; candidate < material_context->environment_count;
                 ++candidate)
                if (material_context->environment[candidate].name != NULL &&
                    strcmp(material_context->environment[candidate].name,
                           last_environment) > 0 &&
                    (selected_input == SIZE_MAX ||
                     strcmp(material_context->environment[candidate].name,
                            material_context->environment[selected_input].name) < 0))
                    selected_input = candidate;
            if (selected_input == SIZE_MAX ||
                !fingerprint_append(&fingerprint, &fingerprint_length,
                                    &fingerprint_capacity,
                                    material_context->environment[selected_input].name,
                                    material_context->environment[selected_input].secret
                                        ? "<secret>"
                                        : material_context->environment[selected_input].value))
                goto done;
            last_environment = material_context->environment[selected_input].name;
            }
        }
        }
    }
    if (material_context == NULL &&
        !fingerprint_append_tool_policy(
            &fingerprint, &fingerprint_length, &fingerprint_capacity, document,
            command_path))
        goto done;
    selected = 0;
    if (material_context == NULL || material_context->tool_identity_count == 0)
        if (!fingerprint_append_tools(&fingerprint, &fingerprint_length,
                                      &fingerprint_capacity, document,
                                      command_path, declared_tools(document),
                                      &selected))
        {
            goto done;
        }
    for (selected = 0; selected < input_count; ++selected) {
        input = SIZE_MAX;
        for (size_t candidate = 0; candidate < input_count; ++candidate)
            if (strcmp(inputs[candidate].name, last_input_name) > 0 &&
                (input == SIZE_MAX ||
                 strcmp(inputs[candidate].name, inputs[input].name) < 0))
                input = candidate;
        if (input == SIZE_MAX)
            goto done;
        last_input_name = inputs[input].name;
        char input_digest[65];
        char field[256];
        if (!cbs_digest_file(inputs[input].path, input_digest) ||
            snprintf(field, sizeof(field), "input.%s", inputs[input].name) >=
                (int)sizeof(field) ||
            !fingerprint_append(&fingerprint, &fingerprint_length,
                                &fingerprint_capacity, field, input_digest))
            goto done;
    }
    ok = cbs_digest_text((const char *)fingerprint, fingerprint_length, output);
done:
    if (file != NULL)
        fclose(file);
    free(source);
    cbs_node_destroy(document);
    cbs_token_list_destroy(&tokens);
    free(fingerprint);
    return ok;
}

int cbs_build_fingerprint(const char *recipe, const char *architecture,
                          const char *command_path, const char *library_path,
                          const CbsInputBinding *inputs, size_t input_count,
                          char output[65]) {
    return cbs_build_fingerprint_with_context(
        recipe, architecture, command_path, library_path, inputs, input_count,
        NULL, output);
}

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
                             const CbsNode *document,
                             const CbsExecutionContext *context,
                             const char *finalize_path,
                             const CbsPrunePolicy *prune_policy,
                             const char *fingerprint,
                             int fingerprint_available) {
    FILE *file;
    char digest[65];
    char fingerprint_reason[256];
    size_t index;

    if (!cbs_digest_text(recipe_text, strlen(recipe_text), digest))
        return 0;
    (void)finalize_path;
    (void)prune_policy;
    fingerprint_reason[0] = '\0';
    if (!fingerprint_available)
        fingerprint_unavailable_reason(document, fingerprint_reason,
                                       sizeof(fingerprint_reason));
    if (!fingerprint_available && fingerprint_reason[0] == '\0')
        snprintf(fingerprint_reason, sizeof(fingerprint_reason),
                 "fingerprint-material-unavailable");
    file = fopen(manifest, "ab");
    if (file == NULL)
        return 0;
    if (fprintf(file, "m recipe %s\n", recipe) < 0 ||
        fprintf(file, "m recipe_sha256 %s\n", digest) < 0 ||
        (fingerprint_available
             ? fprintf(file, "m build_fingerprint %s\n", fingerprint)
             : fprintf(file, "m build_fingerprint unavailable\n")) < 0 ||
        (!fingerprint_available && fingerprint_reason[0] != '\0' &&
         fprintf(file, "m build_fingerprint_reason %s\n",
                 fingerprint_reason) < 0) ||
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

/* Resolve explicit privileged-file declarations once against the staged
 * destination. The manifest checker then performs exact path and mode tests. */
static int collect_privileged_allowances(
    const CbsNode *document, const CbsExecutionContext *context,
    CbsPrivilegedAllowance **allowances_out, size_t *count_out) {
    const CbsNode *package;
    CbsPrivilegedAllowance *allowances = NULL;
    size_t count = 0, index, capacity = 0;

    if (document == NULL || document->child_count != 1 ||
        context == NULL || context->dest == NULL || allowances_out == NULL ||
        count_out == NULL)
        return 0;
    package = document->children[0];
    for (index = 0; index < package->child_count; ++index) {
        const CbsNode *item = package->children[index];
        char *resolved;
        const char *relative;
        char *end;
        unsigned long mode;
        size_t prior;
        if (item->kind != CBS_NODE_PRIVILEGED)
            continue;
        resolved = cbs_resolve_confined_path(item->value, context);
        if (resolved == NULL ||
            strncmp(resolved, context->dest, strlen(context->dest)) != 0 ||
            resolved[strlen(context->dest)] != '/') {
            free(resolved);
            goto failure;
        }
        relative = resolved + strlen(context->dest) + 1;
        mode = strtoul(item->second_value, &end, 8);
        if (*end != '\0' || relative[0] == '\0') {
            free(resolved);
            goto failure;
        }
        for (prior = 0; prior < count; ++prior)
            if (strcmp(allowances[prior].path, relative) == 0) {
                free(resolved);
                goto failure;
            }
        if (count == capacity) {
            size_t next = capacity == 0 ? 4 : capacity * 2;
            CbsPrivilegedAllowance *grown = realloc(
                allowances, next * sizeof(*grown));
            if (grown == NULL) {
                free(resolved);
                goto failure;
            }
            allowances = grown;
            capacity = next;
        }
        allowances[count].path = cbs_duplicate(relative);
        allowances[count].mode = (unsigned)(mode & 07777);
        ++count;
        free(resolved);
    }
    *allowances_out = allowances;
    *count_out = count;
    return 1;

failure:
    for (index = 0; index < count; ++index)
        free((char *)allowances[index].path);
    free(allowances);
    return 0;
}

static void destroy_privileged_allowances(CbsPrivilegedAllowance *allowances,
                                          size_t count) {
    size_t index;
    for (index = 0; index < count; ++index)
        free((char *)allowances[index].path);
    free(allowances);
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

/* Report a staged-tree or packaging-policy refusal separately from a phase
 * process failure so callers can route the two classes independently. */
static void pipeline_manifest_error(const char *recipe, const char *source,
                                    CbsLocation location, const char *step,
                                    const char *detail) {
    char message[768];
    snprintf(message, sizeof(message), "%s: %s", step, detail);
    cbs_diagnostic(recipe, source, location, "error", "CPDL-E4007",
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
    char manifest[4096] = {0};
    CbsExecutionContext policy_context;
    CbsPrivilegedAllowance *allowances = NULL;
    size_t allowance_count = 0;
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
    memset(&policy_context, 0, sizeof(policy_context));
    policy_context.recipe_path = recipe;
    policy_context.recipe_source = text;
    policy_context.src = staged_root;
    policy_context.build = staged_root;
    policy_context.dest = staged_root;
    policy_context.working_directory = staged_root;
    if (ok)
        ok = collect_privileged_allowances(document, &policy_context,
                                           &allowances, &allowance_count);
    snprintf(manifest, sizeof(manifest), "%s/.cbs-manifest", staged_root);
    if (ok)
        ok = cbs_manifest_write_with_license_policy_error(
            staged_root, manifest, declared_license(document), allowances,
            allowance_count, NULL, 0);
    if (ok)
        ok = cbs_cixpkg_write_tree(manifest, staged_root, package_path, "cbs");
    if (manifest[0] != '\0')
        unlink(manifest);
    if (document)
        cbs_node_destroy(document);
    cbs_token_list_destroy(&tokens);
    destroy_privileged_allowances(allowances, allowance_count);
    free(text);
    return ok;
}

/* Package a caller-assembled tree without inventing a second CIXPKG writer. */
static int valid_staged_identity(const CbsPackageIdentity *identity,
                                 const char *license) {
    const unsigned char *cursor;
    if (identity == NULL || identity->name == NULL ||
        identity->version == NULL || identity->architecture == NULL ||
        identity->release <= 0 || identity->name[0] == '\0' ||
        identity->version[0] == '\0' || identity->architecture[0] == '\0' ||
        (license != NULL && strpbrk(license, "\r\n") != NULL))
        return 0;
    cursor = (const unsigned char *)identity->name;
    if (!islower(*cursor) && !isdigit(*cursor))
        return 0;
    while (*++cursor != '\0')
        if (!(islower(*cursor) || isdigit(*cursor) || *cursor == '+' ||
              *cursor == '.' || *cursor == '-'))
            return 0;
    cursor = (const unsigned char *)identity->version;
    while (*cursor != '\0') {
        if (*cursor == '/' || isspace(*cursor))
            return 0;
        ++cursor;
    }
    cursor = (const unsigned char *)identity->architecture;
    if (strcmp((const char *)cursor, "any") == 0 ||
        !(islower(*cursor) || isdigit(*cursor)))
        return 0;
    while (*++cursor != '\0')
        if (!(islower(*cursor) || isdigit(*cursor) || *cursor == '_'))
            return 0;
    return 1;
}

int cbs_package_staged_tree(const char *staged_root,
                            const CbsPackageIdentity *identity,
                            const char *license, const char *package_path) {
    char manifest[4096] = {0};
    char *identity_text = NULL;
    FILE *metadata = NULL;
    struct stat status;
    int ok = 0;

    if (staged_root == NULL || !valid_staged_identity(identity, license) ||
        lstat(staged_root, &status) != 0 || !S_ISDIR(status.st_mode) ||
        S_ISLNK(status.st_mode) || package_path == NULL ||
        package_path[0] == '\0')
        return 0;
    identity_text = cbs_identity_string(identity);
    if (identity_text == NULL ||
        snprintf(manifest, sizeof(manifest), "%s/.cbs-manifest",
                 staged_root) >= (int)sizeof(manifest))
        goto done;
    if (!cbs_manifest_write_with_license_error(staged_root, manifest, license,
                                               NULL, 0))
        goto done;
    metadata = fopen(manifest, "ab");
    if (metadata == NULL ||
        fprintf(metadata, "m cbs_version %s\n", CBS_VERSION) < 0 ||
        fprintf(metadata, "m architecture %s\n", identity->architecture) < 0 ||
        fprintf(metadata, "m identity %s\n", identity_text) < 0 ||
        fprintf(metadata, "m staged_tree 1\n") < 0 || fclose(metadata) != 0) {
        if (metadata != NULL)
            fclose(metadata);
        metadata = NULL;
        goto done;
    }
    metadata = NULL;
    ok = cbs_cixpkg_write_tree(manifest, staged_root, package_path,
                               identity_text);
done:
    if (metadata != NULL)
        fclose(metadata);
    if (manifest[0] != '\0')
        unlink(manifest);
    free(identity_text);
    return ok;
}

/* Execute a recipe, apply policy, and write its standalone artifact. */
int cbs_build_standalone_with_events_policy_path_inputs_tool_identities_result(
    const char *recipe, const char *workspace, const char *package_path,
    const char *architecture, const CbsFetchService *fetch_service,
    const char *cache_directory, CbsFinalizePolicy finalize, void *user,
    const char *firmware_root, const CbsPrunePolicy *prune_policy,
    const char *command_path,
    const char *library_path,
    const CbsInputBinding *inputs, size_t input_count,
    const CbsToolIdentity *tool_identities, size_t tool_identity_count,
    CbsBuildEventSink event_sink, void *event_sink_user,
    char build_fingerprint[65]) {
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
    CbsPrivilegedAllowance *allowances = NULL;
    size_t allowance_count = 0;
    char src[4096], build[4096], dest[4096], cache[4096], manifest[4096],
        manifest_error[512], *package_identity = NULL;
    char tool_command_path[8192];
    char fingerprint[65];
    CbsFingerprintContext fingerprint_context;
    int fingerprint_available = 0;
    unsigned flags = 0;
    int ok;
    if (!recipe || !workspace || !architecture ||
        (command_path != NULL && !cbs_command_path_is_valid(command_path)) ||
        (library_path != NULL && !cbs_library_path_is_valid(library_path)) ||
        (input_count != 0 && inputs == NULL) ||
        (inputs != NULL && !valid_input_binding(inputs, input_count)) ||
        !valid_tool_identities(tool_identities, tool_identity_count))
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
    memset(&fingerprint_context, 0, sizeof(fingerprint_context));
    if (build_fingerprint != NULL)
        build_fingerprint[0] = '\0';
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
            context.inputs = inputs;
            context.input_count = input_count;
            context.tool_identities = tool_identities;
            context.tool_identity_count = tool_identity_count;
            context.jobs = 1;
            context.working_directory = build;
            context.command_path = command_path == NULL
                                       ? CBS_DEFAULT_COMMAND_PATH
                                       : command_path;
            context.library_path = library_path == NULL
                                       ? CBS_DEFAULT_LIBRARY_PATH
                                       : library_path;
            context.tool_policy = declared_tools(document);
            if (!collect_privileged_allowances(document, &context,
                                               &allowances,
                                               &allowance_count)) {
                pipeline_error(recipe, text, document->location,
                               "privileged policy",
                               "cannot resolve privileged file declaration");
                ok = 0;
            }
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
    if (ok) {
        fingerprint_context.firmware_root = context.firmware_root;
        fingerprint_context.finalize_path = finalize == NULL ? NULL : "callback";
        fingerprint_context.tool_directory = context.tool_directory;
        fingerprint_context.environment = context.environment;
        fingerprint_context.environment_count = context.environment_count;
        fingerprint_context.tool_identities = context.tool_identities;
        fingerprint_context.tool_identity_count = context.tool_identity_count;
        if (prune_policy != NULL) {
            fingerprint_context.prune_strip_debug = prune_policy->strip_debug;
            fingerprint_context.prune_drop_static_archives =
                prune_policy->drop_static_archives;
            fingerprint_context.prune_drop_libtool_archives =
                prune_policy->drop_libtool_archives;
        }
        fingerprint_available = cbs_build_fingerprint_with_context(
            recipe, context.arch,
            command_path == NULL ? CBS_DEFAULT_COMMAND_PATH : command_path,
            library_path == NULL ? CBS_DEFAULT_LIBRARY_PATH : library_path,
            context.inputs, context.input_count, &fingerprint_context,
            fingerprint);
    }
    if (build_fingerprint != NULL && fingerprint_available)
        memcpy(build_fingerprint, fingerprint, sizeof(fingerprint));
    if (ok && package_path != NULL) {
        snprintf(manifest, sizeof(manifest), "%s/.cbs-manifest", dest);
        ok = cbs_manifest_write_with_license_policy_error(
            dest, manifest, declared_license(document), allowances,
            allowance_count, manifest_error, sizeof(manifest_error));
        if (!ok)
            pipeline_manifest_error(
                recipe, text, document->location, "manifest",
                manifest_error[0] != '\0' ? manifest_error
                                           : "cannot write staged-tree manifest");
        if (ok && !append_provenance(manifest, recipe, text, &sources,
                                     document,
                                     &context, finalize == NULL ? NULL
                                                                : "callback",
                                     prune_policy, fingerprint,
                                     fingerprint_available)) {
            pipeline_error(recipe, text, document->location, "provenance",
                           "cannot append build provenance");
            ok = 0;
        }
        if (ok) {
            ok = cbs_manifest_collect_with_policy_error(
                dest, &entries, &entry_count, allowances, allowance_count,
                manifest_error, sizeof(manifest_error));
            if (!ok)
                pipeline_manifest_error(
                    recipe, text, document->location, "manifest",
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
    destroy_privileged_allowances(allowances, allowance_count);
    cbs_token_list_destroy(&tokens);
    free(text);
    return ok;
}

int cbs_build_standalone_with_events_policy_path_inputs_tool_identities(
    const char *recipe, const char *workspace, const char *package_path,
    const char *architecture, const CbsFetchService *fetch_service,
    const char *cache_directory, CbsFinalizePolicy finalize, void *user,
    const char *firmware_root, const CbsPrunePolicy *prune_policy,
    const char *command_path, const char *library_path,
    const CbsInputBinding *inputs, size_t input_count,
    const CbsToolIdentity *tool_identities, size_t tool_identity_count,
    CbsBuildEventSink event_sink, void *event_sink_user) {
    return cbs_build_standalone_with_events_policy_path_inputs_tool_identities_result(
        recipe, workspace, package_path, architecture, fetch_service,
        cache_directory, finalize, user, firmware_root, prune_policy,
        command_path, library_path, inputs, input_count, tool_identities,
        tool_identity_count, event_sink, event_sink_user, NULL);
}

int cbs_build_standalone_with_events_policy_path_inputs(
    const char *recipe, const char *workspace, const char *package_path,
    const char *architecture, const CbsFetchService *fetch_service,
    const char *cache_directory, CbsFinalizePolicy finalize, void *user,
    const char *firmware_root, const CbsPrunePolicy *prune_policy,
    const char *command_path, const char *library_path,
    const CbsInputBinding *inputs, size_t input_count,
    CbsBuildEventSink event_sink, void *event_sink_user) {
    return cbs_build_standalone_with_events_policy_path_inputs_tool_identities(
        recipe, workspace, package_path, architecture, fetch_service,
        cache_directory, finalize, user, firmware_root, prune_policy,
        command_path, library_path, inputs, input_count, NULL, 0, event_sink,
        event_sink_user);
}

int cbs_build_standalone_with_events_policy_path(
    const char *recipe, const char *workspace, const char *package_path,
    const char *architecture, const CbsFetchService *fetch_service,
    const char *cache_directory, CbsFinalizePolicy finalize, void *user,
    const char *firmware_root, const CbsPrunePolicy *prune_policy,
    const char *command_path, const char *library_path,
    CbsBuildEventSink event_sink, void *event_sink_user) {
    return cbs_build_standalone_with_events_policy_path_inputs(
        recipe, workspace, package_path, architecture, fetch_service,
        cache_directory, finalize, user, firmware_root, prune_policy,
        command_path, library_path, NULL, 0, event_sink, event_sink_user);
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
