#include "cbs.h"

#ifndef CBS_VERSION
#define CBS_VERSION "unknown"
#endif

const char *cbs_version(void) {
    return CBS_VERSION;
}

unsigned cbs_api_version(void) {
    return CBS_API_VERSION;
}

unsigned cbs_abi_version(void) {
    return CBS_ABI_VERSION;
}

size_t cbs_execution_context_size(void) {
    return sizeof(CbsExecutionContext);
}

/* Adapter for requests delegated to an embedding daemon. */
/* Validate arguments, then forward a daemon request to the embedder. */
int cbs_daemon_request(CbsDaemonRequest request, void *user,
                       const char *operation, const char *payload,
                       char *response, size_t response_size) {
    if (!request || !operation || !payload || !response || response_size == 0)
        return 0;
    return request(operation, payload, response, response_size, user);
}
