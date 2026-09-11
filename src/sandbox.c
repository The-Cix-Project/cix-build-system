/* Optional embedding seam for entering and leaving a build sandbox. */
#include "cbs.h"
/* Run one operation between optional enter and leave hooks. */
int cbs_sandbox_run(CbsSandboxHook enter, CbsSandboxHook leave,
                    const char *root, void *user) {
    if (!enter || !leave || !root)
        return 0;
    if (!enter(root, user))
        return 0;
    if (!leave(root, user))
        return 0;
    return 1;
}
