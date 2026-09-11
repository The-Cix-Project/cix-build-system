/* Source transport adapters, including the command-line TLS fetch service. */
#define _POSIX_C_SOURCE 200809L

#include "cbs.h"

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

typedef void Curl;
typedef int CurlCode;
typedef Curl *(*CurlEasyInit)(void);
typedef CurlCode (*CurlEasySetopt)(Curl *, int, ...);
typedef CurlCode (*CurlEasyPerform)(Curl *);
typedef void (*CurlEasyCleanup)(Curl *);

enum {
    CURLOPT_URL = 10002,
    CURLOPT_WRITEDATA = 10001,
    CURLOPT_WRITEFUNCTION = 20011,
    CURLOPT_ERRORBUFFER = 10010,
    CURLOPT_FOLLOWLOCATION = 52,
    CURLOPT_MAXREDIRS = 68,
    CURLOPT_FAILONERROR = 45,
    CURLOPT_SSL_VERIFYPEER = 64,
    CURLOPT_SSL_VERIFYHOST = 81,
    CURLOPT_CONNECTTIMEOUT_MS = 222,
    CURLOPT_TIMEOUT_MS = 155,
    CURLOPT_USERAGENT = 10018,
    CURLOPT_PROTOCOLS = 181,
    CURLOPT_REDIR_PROTOCOLS = 182,
    CURLOPT_CAINFO = 10065
};

typedef struct {
    /* Dynamically loaded libcurl handle. */
    void *library;
    /* Resolved libcurl entry points. */
    CurlEasyInit easy_init;
    CurlEasySetopt easy_setopt;
    CurlEasyPerform easy_perform;
    CurlEasyCleanup easy_cleanup;
    /* Optional CA bundle selected by the embedder. */
    const char *ca_file;
} CurlApi;

/* Write downloaded bytes to the destination stream. */
static size_t write_file(const void *data, size_t size, size_t count,
                         void *opaque) {
    return fwrite(data, size, count, opaque);
}

/* Load the small libcurl API surface used by CBS at runtime. */
static int load_api(CurlApi *api, char *error, size_t error_size) {
    memset(api, 0, sizeof(*api));
    api->library = dlopen("libcurl.so.4", RTLD_NOW | RTLD_LOCAL);
    if (api->library == NULL)
        api->library = dlopen("libcurl.so", RTLD_NOW | RTLD_LOCAL);
    if (api->library == NULL) {
        snprintf(error, error_size, "libcurl is not available: %s", dlerror());
        return 0;
    }
    *(void **)(&api->easy_init) = dlsym(api->library, "curl_easy_init");
    *(void **)(&api->easy_setopt) = dlsym(api->library, "curl_easy_setopt");
    *(void **)(&api->easy_perform) = dlsym(api->library, "curl_easy_perform");
    *(void **)(&api->easy_cleanup) = dlsym(api->library, "curl_easy_cleanup");
    if (!api->easy_init || !api->easy_setopt || !api->easy_perform ||
        !api->easy_cleanup) {
        snprintf(error, error_size, "libcurl is missing the easy interface");
        dlclose(api->library);
        memset(api, 0, sizeof(*api));
        return 0;
    }
    return 1;
}

/* Fetch one URL into a temporary file and publish it atomically. */
static int curl_fetch(const char *url, const char *destination, void *opaque,
                      char *error, size_t error_size) {
    CurlApi *api = opaque;
    Curl *handle;
    FILE *file;
    char curl_error[256] = {0};
    CurlCode result;

    file = fopen(destination, "wb");
    if (file == NULL) {
        snprintf(error, error_size, "cannot open temporary destination");
        return 0;
    }
    handle = api->easy_init();
    if (handle == NULL) {
        fclose(file);
        snprintf(error, error_size, "curl_easy_init failed");
        return 0;
    }
    api->easy_setopt(handle, CURLOPT_URL, url);
    api->easy_setopt(handle, CURLOPT_WRITEDATA, file);
    api->easy_setopt(handle, CURLOPT_WRITEFUNCTION, write_file);
    api->easy_setopt(handle, CURLOPT_ERRORBUFFER, curl_error);
    api->easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
    api->easy_setopt(handle, CURLOPT_MAXREDIRS, 5L);
    api->easy_setopt(handle, CURLOPT_FAILONERROR, 1L);
    api->easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 1L);
    api->easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 2L);
    if (api->ca_file != NULL)
        api->easy_setopt(handle, CURLOPT_CAINFO, api->ca_file);
    api->easy_setopt(handle, CURLOPT_CONNECTTIMEOUT_MS, 15000L);
    api->easy_setopt(handle, CURLOPT_TIMEOUT_MS, 120000L);
    api->easy_setopt(handle, CURLOPT_USERAGENT, "cbs/0.1");
    api->easy_setopt(handle, CURLOPT_PROTOCOLS, 3L);
    api->easy_setopt(handle, CURLOPT_REDIR_PROTOCOLS, 3L);
    result = api->easy_perform(handle);
    api->easy_cleanup(handle);
    if (fclose(file) != 0)
        result = 1;
    if (result != 0) {
        snprintf(error, error_size, "%s",
                 curl_error[0] == '\0' ? "libcurl transfer failed"
                                       : curl_error);
        return 0;
    }
    return 1;
}

/* Create the default HTTPS source-fetch service. */
int cbs_cli_fetch_service(CbsFetchService *service, char *error,
                          size_t error_size) {
    return cbs_cli_fetch_service_with_ca(service, error, error_size, NULL);
}

/* Create an HTTPS fetch service with an optional private CA bundle. */
int cbs_cli_fetch_service_with_ca(CbsFetchService *service, char *error,
                                  size_t error_size, const char *ca_file) {
    static CurlApi api;

    if (service == NULL || error == NULL || error_size == 0)
        return 0;
    if (!load_api(&api, error, error_size))
        return 0;
    api.ca_file = ca_file;
    service->fetch = curl_fetch;
    service->user = &api;
    return 1;
}
