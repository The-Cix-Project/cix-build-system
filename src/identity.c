/* Canonical package identity and identity-derived metadata helpers. */
#include "cbs.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Check that an architecture is a concrete lower-case target name. */
static int valid_architecture(const char *architecture) {
    const unsigned char *cursor = (const unsigned char *)architecture;

    if (architecture == NULL || *cursor == '\0' ||
        !((cursor[0] >= 'a' && cursor[0] <= 'z') || isdigit(cursor[0])) ||
        strcmp(architecture, "any") == 0)
        return 0;
    while (*++cursor != '\0')
        if (!((*cursor >= 'a' && *cursor <= 'z') || isdigit(*cursor) ||
              *cursor == '_'))
            return 0;
    return 1;
}

/* Read package identity fields and combine them with the target architecture.
 */
int cbs_identity_from_document(const CbsNode *document,
                               const char *architecture,
                               CbsPackageIdentity *identity) {
    const CbsNode *package;
    size_t index;

    if (document == NULL || identity == NULL ||
        document->kind != CBS_NODE_DOCUMENT || document->child_count != 1 ||
        document->children[0]->kind != CBS_NODE_PACKAGE ||
        !valid_architecture(architecture))
        return 0;
    package = document->children[0];
    memset(identity, 0, sizeof(*identity));
    identity->name = package->value;
    identity->architecture = architecture;
    for (index = 0; index < package->child_count; ++index) {
        const CbsNode *item = package->children[index];
        if (item->kind == CBS_NODE_VERSION)
            identity->version = item->value;
        else if (item->kind == CBS_NODE_RELEASE)
            identity->release = item->number;
        else if (item->kind == CBS_NODE_ARCHITECTURE)
            return 0;
    }
    return identity->name != NULL && identity->name[0] != '\0' &&
           identity->version != NULL && identity->version[0] != '\0' &&
           identity->release > 0;
}

/* Format the canonical identity used in artifact names and metadata. */
char *cbs_identity_string(const CbsPackageIdentity *identity) {
    int length =
        snprintf(NULL, 0, "%s-%s-%ld-%s", identity->name, identity->version,
                 identity->release, identity->architecture);
    char *result;

    if (length < 0)
        return NULL;
    result = cbs_allocate((size_t)length + 1);
    snprintf(result, (size_t)length + 1, "%s-%s-%ld-%s", identity->name,
             identity->version, identity->release, identity->architecture);
    return result;
}

/* Append the CIXPKG suffix to the canonical identity. */
char *cbs_identity_artifact_filename(const CbsPackageIdentity *identity) {
    char *canonical = cbs_identity_string(identity);
    size_t length;
    char *filename;

    if (canonical == NULL)
        return NULL;
    length = strlen(canonical);
    filename = cbs_allocate(length + sizeof(".cixpkg"));
    memcpy(filename, canonical, length);
    memcpy(filename + length, ".cixpkg", sizeof(".cixpkg"));
    free(canonical);
    return filename;
}

/* Create the stable newline-terminated identity digest input. */
char *cbs_identity_digest_metadata(const CbsPackageIdentity *identity,
                                   size_t *length) {
    static const char prefix[] = "identity=";
    char *canonical = cbs_identity_string(identity);
    size_t canonical_length;
    char *metadata;

    if (canonical == NULL || length == NULL) {
        free(canonical);
        return NULL;
    }
    canonical_length = strlen(canonical);
    *length = sizeof(prefix) - 1 + canonical_length + 1;
    metadata = cbs_allocate(*length + 1);
    memcpy(metadata, prefix, sizeof(prefix) - 1);
    memcpy(metadata + sizeof(prefix) - 1, canonical, canonical_length);
    metadata[*length - 1] = '\n';
    metadata[*length] = '\0';
    free(canonical);
    return metadata;
}

void cbs_identity_apply_execution_context(const CbsPackageIdentity *identity,
                                          CbsExecutionContext *context) {
    context->name = identity->name;
    context->version = identity->version;
    context->release = identity->release;
    context->arch = identity->architecture;
}
