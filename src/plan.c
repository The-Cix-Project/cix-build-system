/* Build-plan construction, metadata extraction, and phase event dispatch. */
#include "cbs.h"
#include <string.h>

/* Collect the five ordered phase slots from a validated package. */
int cbs_build_plan(const CbsNode *document, CbsBuildPlan *plan) {
    const CbsNode *package;
    size_t index;
    if (document == NULL || plan == NULL || document->child_count != 1)
        return 0;
    memset(plan, 0, sizeof(*plan));
    package = document->children[0];
    for (index = 0; index < package->child_count; ++index)
        if (package->children[index]->kind == CBS_NODE_PHASE) {
            if (plan->count >= CBS_MAX_PHASES)
                return 0;
            plan->phases[plan->count++] = package->children[index];
        }
    return plan->count > 0;
}

/* Collect embedder-facing build metadata from the package AST. */
int cbs_build_metadata(const CbsNode *document, CbsBuildMetadata *metadata) {
    const CbsNode *package;
    size_t index;
    if (document == NULL || metadata == NULL || document->child_count != 1)
        return 0;
    memset(metadata, 0, sizeof(*metadata));
    package = document->children[0];
    for (index = 0; index < package->child_count; ++index) {
        const CbsNode *item = package->children[index];
        if (item->kind == CBS_NODE_BUILD_IMAGE)
            metadata->build_image = item->value;
        else if (item->kind == CBS_NODE_UPSTREAM)
            metadata->upstream = item->value;
        else if (item->kind == CBS_NODE_CAPABILITY)
            ++metadata->capability_count;
        else if (item->kind == CBS_NODE_TOOLCHAIN) {
            metadata->toolchain = item->value;
            if (item->child_count > 0)
                metadata->toolchain_reason = item->children[0]->value;
        }
    }
    return 1;
}

/* Execute phases in order and emit optional begin/end events. */
int cbs_execute_plan(const CbsBuildPlan *plan,
                     const CbsExecutionContext *context) {
    size_t index;
    if (plan == NULL || context == NULL)
        return 0;
    for (index = 0; index < plan->count; ++index) {
        if (context->phase_event != NULL &&
            !context->phase_event("phase-begin", plan->phases[index]->name, 0,
                                  context->phase_event_user))
            return 0;
        if (!cbs_execute_block(plan->phases[index], context)) {
            if (context->phase_event != NULL)
                context->phase_event("phase-end", plan->phases[index]->name, 1,
                                     context->phase_event_user);
            return 0;
        }
        if (context->phase_event != NULL &&
            !context->phase_event("phase-end", plan->phases[index]->name, 0,
                                  context->phase_event_user))
            return 0;
    }
    return 1;
}
