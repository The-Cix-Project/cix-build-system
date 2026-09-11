/* Optional embedding seam for service health checks. */
#include "cbs.h"
/* Invoke the optional service health callback. */
int cbs_service_health(CbsHealthCheck check, void *user) {
    return check != NULL && check(user);
}
