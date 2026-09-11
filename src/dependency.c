/* Selection of declared dependencies for each execution phase. */
#include "cbs.h"

#include <stdlib.h>
#include <string.h>

static int role_selected(const char *role, const char *phase) {
    if (strcmp(role, "bootstrap") == 0 || strcmp(role, "build") == 0)
        return strcmp(phase, "install") != 0;
    if (strcmp(role, "test") == 0)
        return strcmp(phase, "check") == 0;
    return strcmp(phase, "install") == 0;
}

int cbs_dependencies_for_phase(const CbsNode *document, const char *phase,
                               CbsDependencySet *set) {
    const CbsNode *package;
    size_t index, group_index, dependency_index, count = 0;

    memset(set, 0, sizeof(*set));
    if (document == NULL || document->child_count != 1 || phase == NULL)
        return 0;
    package = document->children[0];
    for (index = 0; index < package->child_count; ++index) {
        const CbsNode *
            requires
        = package->children[index];
        if (requires->kind != CBS_NODE_REQUIRES)
            continue;
        for (group_index = 0; group_index < requires->child_count;
             ++group_index) {
            const CbsNode *group =
                requires
                ->children[group_index];
            if (role_selected(group->name, phase))
                count += group->child_count;
        }
    }
    set->items = cbs_allocate(count * sizeof(*set->items));
    set->count = count;
    count = 0;
    for (index = 0; index < package->child_count; ++index) {
        const CbsNode *
            requires
        = package->children[index];
        if (requires->kind != CBS_NODE_REQUIRES)
            continue;
        for (group_index = 0; group_index < requires->child_count;
             ++group_index) {
            const CbsNode *group =
                requires
                ->children[group_index];
            if (!role_selected(group->name, phase))
                continue;
            for (dependency_index = 0; dependency_index < group->child_count;
                 ++dependency_index) {
                const CbsNode *dependency = group->children[dependency_index];
                set->items[count].role = group->name;
                set->items[count].kind = dependency->name;
                set->items[count].name = dependency->value;
                ++count;
            }
        }
    }
    return 1;
}

void cbs_dependency_set_destroy(CbsDependencySet *set) {
    free(set->items);
    memset(set, 0, sizeof(*set));
}

int cbs_dependency_set_contains(const CbsDependencySet *set, const char *kind,
                                const char *name) {
    size_t index;
    for (index = 0; index < set->count; ++index)
        if (strcmp(set->items[index].kind, kind) == 0 &&
            strcmp(set->items[index].name, name) == 0)
            return 1;
    return 0;
}
