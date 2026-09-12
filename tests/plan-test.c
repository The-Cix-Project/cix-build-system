/* Regression tests for phase capacity and execution event ordering. */
#include "cbs.h"

#include <stdio.h>
#include <string.h>

static int events;
static int structured_events;
static unsigned long long last_sequence;
static int phase_event(const char *event, const char *phase, int status,
                       void *user) {
    (void)user;
    if (events >= 2)
        return 1;
    if (events == 0 && strcmp(event, "phase-begin") == 0 &&
        strcmp(phase, "build") == 0 && status == 0) {
        events++;
        return 1;
    }
    if (events == 1 && strcmp(event, "phase-end") == 0 &&
        strcmp(phase, "build") == 0 && status == 0) {
        events++;
        return 1;
    }
    return 0;
}

static int build_event(const CbsBuildEvent *event, void *user) {
    (void)user;
    if (event == NULL || event->version != 1 || event->type == NULL ||
        event->sequence <= last_sequence)
        return 0;
    if (strcmp(event->type, "phase-begin") != 0 &&
        strcmp(event->type, "phase-end") != 0)
        return 0;
    last_sequence = event->sequence;
    structured_events++;
    return 1;
}

/* Verify phase capacity, overflow rejection, and event ordering. */
int main(void) {
    CbsLocation location = {"plan-test", 1, 1, 0};
    CbsNode *document = cbs_node_create(CBS_NODE_DOCUMENT, location);
    CbsNode *package = cbs_node_create(CBS_NODE_PACKAGE, location);
    CbsBuildPlan plan;
    CbsExecutionContext context;
    size_t index;

    cbs_node_add(document, package);
    memset(&context, 0, sizeof(context));
    context.phase_event = phase_event;
    context.event_sink = build_event;
    for (index = 0; index < CBS_MAX_PHASES; ++index)
        cbs_node_add(package, cbs_node_create(CBS_NODE_PHASE, location));
    package->children[0]->name = cbs_duplicate("build");
    if (!cbs_build_plan(document, &plan) || plan.count != CBS_MAX_PHASES) {
        cbs_node_destroy(document);
        return 1;
    }
    if (!cbs_execute_plan(&plan, &context) || events != 2 ||
        structured_events != CBS_MAX_PHASES * 2) {
        cbs_node_destroy(document);
        return 1;
    }
    cbs_node_add(package, cbs_node_create(CBS_NODE_PHASE, location));
    if (cbs_build_plan(document, &plan)) {
        cbs_node_destroy(document);
        return 1;
    }
    cbs_node_destroy(document);
    puts("build plan tests: PASS (phase capacity is explicit)");
    return 0;
}
