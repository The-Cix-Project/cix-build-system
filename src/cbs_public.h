#ifndef CBS_PUBLIC_H
#define CBS_PUBLIC_H

/* Stable embedding surface for libcbs. Recipe-parser internals live in the
 * repository-private cbs.h and are deliberately not part of this header. */
#include <stddef.h>
#include <stdio.h>

typedef struct CbsNode CbsNode;

typedef struct {
    const char *name;
    const char *path;
} CbsNamedSource;

typedef struct {
    const char *name;
    const char *value;
} CbsEnvironmentBinding;

typedef struct {
    const char *name;
    char *value;
} CbsOutputBinding;

typedef struct {
    unsigned version;
    const char *type;
    unsigned long long sequence;
    unsigned long long timestamp_ms;
    const char *build_id;
    const char *package_name;
    const char *package_version;
    long package_release;
    const char *arch;
    const char *phase;
    const char *command;
    const char *arguments;
    const char *environment_names;
    const char *working_directory;
    const char *log_path;
    const char *message;
    int status;
    long duration_ms;
    unsigned long long stdout_bytes;
    unsigned long long stderr_bytes;
    unsigned long long cpu_ms;
    unsigned long long max_memory_bytes;
    unsigned long long source_bytes;
    unsigned long long fetch_duration_ms;
    unsigned long long tree_bytes;
    unsigned long long tree_files;
    unsigned long long artifact_bytes;
} CbsBuildEvent;

typedef int (*CbsBuildEventSink)(const CbsBuildEvent *, void *);

typedef struct {
    unsigned version;
    unsigned long long event_count;
    unsigned long long phase_count;
    unsigned long long command_count;
    unsigned long long cache_hits;
    unsigned long long cache_misses;
    unsigned long long sources_fetched;
    unsigned long long source_bytes;
    unsigned long long fetch_duration_ms;
    unsigned long long tree_bytes;
    unsigned long long tree_files;
    unsigned long long artifact_bytes;
    unsigned long long cpu_ms;
    unsigned long long max_memory_bytes;
    unsigned long long duration_ms;
    unsigned long long stdout_bytes;
    unsigned long long stderr_bytes;
    int status;
    char artifact_path[4096];
    char failure_message[1024];
} CbsBuildReport;

typedef struct {
    const char *recipe_path;
    const char *recipe_source;
    const char *name;
    const char *version;
    long release;
    const char *arch;
    const char *compiler;
    const char *src;
    const char *build;
    const char *dest;
    long jobs;
    const char *working_directory;
    const char *log_directory;
    const char *current_log_path;
    const char *current_arguments;
    const char *current_environment_names;
    const CbsNamedSource *sources;
    size_t source_count;
    const CbsEnvironmentBinding *environment;
    size_t environment_count;
    CbsOutputBinding *output_bindings;
    size_t output_binding_count;
    size_t output_binding_capacity;
    int (*phase_event)(const char *, const char *, int, void *);
    void *phase_event_user;
    CbsBuildEventSink event_sink;
    void *event_sink_user;
    const char *build_id;
    const char *current_phase;
    unsigned long long event_sequence;
    unsigned long long current_cpu_ms;
    unsigned long long current_max_memory_bytes;
    unsigned long long current_source_bytes;
    unsigned long long current_fetch_duration_ms;
    unsigned long long current_tree_bytes;
    unsigned long long current_tree_files;
    unsigned long long current_artifact_bytes;
    struct {
        long address_space_mb;
        long file_size_mb;
        long cpu_seconds;
        long open_files;
        long processes;
    } limits;
} CbsExecutionContext;

typedef int (*CbsFetchFunction)(const char *, const char *, void *, char *,
                                size_t);
typedef struct {
    CbsFetchFunction fetch;
    void *user;
} CbsFetchService;

typedef struct {
    const char *path;
    char type;
    unsigned mode;
    unsigned uid;
    unsigned gid;
    unsigned long long size;
    const char *digest;
    const char *target;
} CbsManifestEntry;

typedef int (*CbsFinalizePolicy)(const char *, void *);
typedef int (*CbsDependencyObserver)(const char *, void *);
typedef int (*CbsSignatureVerifier)(const unsigned char *, size_t, void *);

int cbs_build_event_jsonl(const CbsBuildEvent *, void *);
int cbs_build_event_human(const CbsBuildEvent *, void *);
void cbs_build_report_init(CbsBuildReport *);
int cbs_build_report_consume(const CbsBuildEvent *, void *);
int cbs_build_report_write_json(const CbsBuildReport *, FILE *);

void *cbs_allocate(size_t);
void *cbs_reallocate(void *, size_t);
char *cbs_duplicate(const char *);
char *cbs_duplicate_range(const char *, size_t);

int cbs_cli_fetch_service(CbsFetchService *, char *, size_t);
int cbs_cli_fetch_service_with_ca(CbsFetchService *, char *, size_t,
                                  const char *);
int cbs_build_standalone(const char *, const char *, const char *, const char *,
                         const CbsFetchService *);
int cbs_build_standalone_with_cache(const char *, const char *, const char *,
                                    const char *, const CbsFetchService *,
                                    const char *);
int cbs_build_standalone_with_cache_policy(
    const char *, const char *, const char *, const char *,
    const CbsFetchService *, const char *, CbsFinalizePolicy, void *);
int cbs_build_standalone_with_events(
    const char *, const char *, const char *, const char *,
    const CbsFetchService *, const char *, CbsFinalizePolicy, void *,
    CbsBuildEventSink, void *);
int cbs_build_package(const char *, const char *, const char *);

int cbs_workspace_prepare(const char *);
int cbs_manifest_collect(const char *, CbsManifestEntry **, size_t *);
void cbs_manifest_entries_destroy(CbsManifestEntry *, size_t);
int cbs_manifest_write(const char *, const char *);
int cbs_manifest_compare(const void *, const void *);
int cbs_cixpkg_verify_tree(const char *, char *, size_t);
int cbs_cixpkg_extract(const char *, const char *);
int cbs_cixpkg_write_tree(const char *, const char *, const char *,
                          const char *);
int cbs_cixpkg_write_tree_with_flags(const char *, const char *, const char *,
                                     const char *, unsigned);
int cbs_cixpkg_compress(const char *, const char *);
int cbs_cixpkg_decompress(const char *, const char *);
int cbs_compare_files(const char *, const char *);
int cbs_install_atomic(const char *, const char *, unsigned);

int cbs_is_forbidden_executable(const char *);
int cbs_is_forbidden_compiler(const char *);
int cbs_observe_dependencies(CbsDependencyObserver, const char *, void *);
int cbs_verify_signature(CbsSignatureVerifier, const char *, void *);

#endif
