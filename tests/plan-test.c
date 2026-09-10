#include "cbs.h"

#include <stdio.h>

int main(void)
{
    CbsLocation location = {"plan-test", 1, 1, 0};
    CbsNode *document = cbs_node_create(CBS_NODE_DOCUMENT, location);
    CbsNode *package = cbs_node_create(CBS_NODE_PACKAGE, location);
    CbsBuildPlan plan;
    size_t index;

    cbs_node_add(document, package);
    for (index = 0; index < CBS_MAX_PHASES; ++index)
        cbs_node_add(package, cbs_node_create(CBS_NODE_PHASE, location));
    if (!cbs_build_plan(document, &plan) || plan.count != CBS_MAX_PHASES) {
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
