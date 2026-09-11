/* Adapter for requests delegated to an embedding daemon. */
#include "cbs.h"
/* Validate arguments, then forward a daemon request to the embedder. */
int cbs_daemon_request(CbsDaemonRequest request, void *user,
                       const char *operation, const char *payload,
                       char *response, size_t response_size) {
    if (!request || !operation || !payload || !response || response_size == 0)
        return 0;
    return request(operation, payload, response, response_size, user);
}
