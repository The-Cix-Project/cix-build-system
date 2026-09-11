/* Optional embedding seam for service health checks. */
#include "cbs.h"
int cbs_service_health(CbsHealthCheck check, void *user) {
    return check != NULL && check(user);
}
