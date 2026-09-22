/* Build-plan construction, metadata extraction, and phase event dispatch. */
#include "cbs.h"

#include <string.h>
#include <time.h>

static long plan_elapsed_ms(const struct timespec *start,
                            const struct timespec *end) {
    return (long)(end->tv_sec - start->tv_sec) * 1000L +
           (end->tv_nsec - start->tv_nsec) / 1000000L;
}

static int is_check_phase(const CbsNode *phase) {
    return phase->name != NULL && strcmp(phase->name, "check") == 0;
}

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
        else if (item->kind == CBS_NODE_LICENSE)
            metadata->license = item->value;
        else if (item->kind == CBS_NODE_TOOLCHAIN) {
            metadata->toolchain = item->value;
            if (item->child_count > 0)
                metadata->toolchain_reason = item->children[0]->value;
        }
    }
    return 1;
}

/* Execute phases in order and emit optional begin/end events. */
static int execute_phase(const CbsNode *phase,
                         CbsExecutionContext *context) {
    CbsExecutionContext *mutable_context = (CbsExecutionContext *)context;
    struct timespec phase_start;
    struct timespec phase_end;
    mutable_context->current_phase = phase->name;
        clock_gettime(CLOCK_MONOTONIC, &phase_start);
        if (context->phase_event != NULL &&
            !context->phase_event("phase-begin", phase->name, 0,
                                  context->phase_event_user))
            return 0;
        if (!cbs_emit_build_event(context, "phase-begin",
                                  phase->name, NULL, NULL, 0, 0,
                                  0, 0))
            return 0;
        if (!cbs_execute_block(phase, context)) {
            clock_gettime(CLOCK_MONOTONIC, &phase_end);
            if (context->phase_event != NULL)
                context->phase_event("phase-end", phase->name, 1,
                                     context->phase_event_user);
            cbs_emit_build_event(context, "phase-end",
                                 phase->name, NULL,
                                 "phase failed", 1,
                                 plan_elapsed_ms(&phase_start, &phase_end), 0,
                                 0);
            return 0;
        }
        if (context->phase_event != NULL &&
            !context->phase_event("phase-end", phase->name, 0,
                                  context->phase_event_user))
            return 0;
        clock_gettime(CLOCK_MONOTONIC, &phase_end);
        if (!cbs_emit_build_event(context, "phase-end",
                                  phase->name, NULL, NULL, 0,
                                  plan_elapsed_ms(&phase_start, &phase_end), 0,
                                  0))
            return 0;
    return 1;
}

int cbs_execute_plan(const CbsBuildPlan *plan,
                     const CbsExecutionContext *context) {
    CbsExecutionContext *mutable_context = (CbsExecutionContext *)context;
    size_t index;
    if (plan == NULL || context == NULL)
        return 0;
    /* A check validates the installed tree, so defer it until every other
     * declared phase (especially install) has completed. */
    for (index = 0; index < plan->count; ++index)
        if (!is_check_phase(plan->phases[index]) &&
            !execute_phase(plan->phases[index], mutable_context))
            return 0;
    for (index = 0; index < plan->count; ++index)
        if (is_check_phase(plan->phases[index]) &&
            !execute_phase(plan->phases[index], mutable_context))
            return 0;
    return 1;
}
