#ifndef CBS_H
#define CBS_H

/*
 * Public interface for the Cix Build System.
 *
 * The declarations are grouped by responsibility: CPDL representation and
 * execution, source preparation, manifests and packages, then embedding
 * seams.  The implementation never treats recipe text as shell input.
 */

#include <stddef.h>
#include <stdio.h>

typedef struct {
    /* Source filename or logical diagnostic origin. */
    const char *path;
    /* One-based source line. */
    size_t line;
    /* One-based source column. */
    size_t column;
    /* Zero-based byte offset in the source. */
    size_t offset;
} CbsLocation;

typedef enum {
    CBS_TOKEN_EOF,
    CBS_TOKEN_WORD,
    CBS_TOKEN_STRING,
    CBS_TOKEN_BLOCK_STRING,
    CBS_TOKEN_INTEGER,
    CBS_TOKEN_MODE,
    CBS_TOKEN_DURATION,
    CBS_TOKEN_CBS_VALUE,
    CBS_TOKEN_LBRACE,
    CBS_TOKEN_RBRACE,
    CBS_TOKEN_EQUAL
} CbsTokenKind;

typedef struct {
    /* Token category assigned by the lexer. */
    CbsTokenKind kind;
    /* Token text owned by the token list. */
    char *text;
    /* Location where the token starts. */
    CbsLocation location;
} CbsToken;

typedef struct {
    /* Allocated token storage. */
    CbsToken *items;
    /* Number of initialized tokens. */
    size_t count;
    /* Allocated token capacity. */
    size_t capacity;
} CbsTokenList;

typedef enum {
    CBS_NODE_DOCUMENT,
    CBS_NODE_PACKAGE,
    CBS_NODE_VERSION,
    CBS_NODE_RELEASE,
    CBS_NODE_FORMAT,
    CBS_NODE_LICENSE,
    CBS_NODE_ARCHITECTURE,
    CBS_NODE_SOURCES,
    CBS_NODE_SOURCE,
    CBS_NODE_URL,
    CBS_NODE_SHA256,
    CBS_NODE_REQUIRES,
    CBS_NODE_DEPENDENCY_GROUP,
    CBS_NODE_DEPENDENCY,
    CBS_NODE_PHASE,
    CBS_NODE_RUN,
    CBS_NODE_ARGUMENT,
    CBS_NODE_RUN_ENV,
    CBS_NODE_RUN_JOBS,
    CBS_NODE_RUN_TIMEOUT,
    CBS_NODE_RUN_EXPECT,
    CBS_NODE_RUN_STDOUT_ASSERT,
    CBS_NODE_RUN_STDOUT_BIND,
    CBS_NODE_RUN_STDOUT_FILE,
    CBS_NODE_ALLOW_FAILURE,
    CBS_NODE_ENV,
    CBS_NODE_CD,
    CBS_NODE_MKDIR,
    CBS_NODE_COPY,
    CBS_NODE_MOVE,
    CBS_NODE_REMOVE,
    CBS_NODE_SYMLINK,
    CBS_NODE_WRITE,
    CBS_NODE_CHMOD,
    CBS_NODE_EXTRACT,
    CBS_NODE_MATERIALIZE,
    CBS_NODE_REPLACE,
    CBS_NODE_INSERT,
    CBS_NODE_TRUNCATE,
    CBS_NODE_GLOB_BIND,
    CBS_NODE_LINKS,
    CBS_NODE_PATCH,
    CBS_NODE_REQUIRE,
    CBS_NODE_LIST,
    CBS_NODE_ON_FAIL,
    CBS_NODE_PROPERTY,
    CBS_NODE_BUILD_IMAGE,
    CBS_NODE_CAPABILITY,
    CBS_NODE_TOOLCHAIN,
    CBS_NODE_UPSTREAM,
    CBS_NODE_METADATA,
    /* stage library "NAME" into PATH: ship a build-dependency library. */
    CBS_NODE_STAGE
} CbsNodeKind;

typedef struct CbsNode CbsNode;

struct CbsNode {
    /* AST node category. */
    CbsNodeKind kind;
    /* Location of the syntax that created this node. */
    CbsLocation location;
    /* Identifier or keyword associated with the node. */
    char *name;
    /* Primary string value associated with the node. */
    char *value;
    /* Secondary string value used by selected operations. */
    char *second_value;
    /* Integer or mode value parsed from the source. */
    long number;
    /* Primary boolean option. */
    int flag;
    /* Secondary boolean option. */
    int second_flag;
    /* Whether a source-edit target is a glob selector. */
    int selector_glob;
    /* Whether an insert operation writes before, rather than after, a match. */
    int insert_before;
    /* Child nodes in source order. */
    CbsNode **children;
    /* Number of initialized child pointers. */
    size_t child_count;
    /* Allocated child-pointer capacity. */
    size_t child_capacity;
};

typedef struct {
    /* Recipe path used in diagnostics. */
    const char *path;
    /* Complete normalized recipe source. */
    const char *source;
    /* Source length in bytes. */
    size_t source_length;
    /* Tokens being consumed by the parser. */
    CbsTokenList tokens;
    /* Index of the next token to consume. */
    size_t cursor;
    /* Set when parsing has emitted a fatal diagnostic. */
    int failed;
} CbsParser;

typedef enum {
    CBS_DIAG_LEX,
    CBS_DIAG_PARSE,
    CBS_DIAG_VALIDATION,
    CBS_DIAG_RUNTIME,
    CBS_DIAG_SOURCE,
    CBS_DIAG_INTERNAL
} CbsDiagCategory;

typedef struct {
    /* Human-readable source name used in diagnostics and interpolation. */
    const char *name;
    /* Source path or URL recorded for this named source. */
    const char *path;
} CbsNamedSource;

typedef struct {
    const char *name;
    char *value;
} CbsOutputBinding;

typedef CbsOutputBinding CbsGlobBinding;

typedef struct {
    /* Environment variable name visible to a phase. */
    const char *name;
    /* Environment value supplied by the recipe or embedder. */
    const char *value;
} CbsEnvironmentBinding;

typedef struct {
    /* Package name declared by the recipe. */
    const char *name;
    /* Package version declared by the recipe. */
    const char *version;
    /* Numeric release revision used for package identity. */
    long release;
    /* Target architecture declared by the recipe. */
    const char *architecture;
} CbsPackageIdentity;

typedef struct {
    /* Source kind: main or extra. */
    const char *kind;
    /* Logical source name used by interpolation. */
    const char *name;
    /* Candidate URLs tried in declaration order. */
    const char **urls;
    /* Number of candidate URLs. */
    size_t url_count;
    /* Declared lowercase SHA-256 digest. */
    const char *sha256;
    /* Verified cache/workspace path, when preparation succeeded. */
    char *verified_path;
} CbsSource;

typedef struct {
    /* All declared source records. */
    CbsSource *items;
    /* Number of source records. */
    size_t count;
    /* Interpolation bindings derived from verified sources. */
    CbsNamedSource *bindings;
} CbsSourceSet;

typedef int (*CbsFetchFunction)(const char *url, const char *destination,
                                void *user, char *error, size_t error_size);

typedef struct {
    /* Callback used to fetch one URL. */
    CbsFetchFunction fetch;
    /* Opaque state passed to the callback. */
    void *user;
} CbsFetchService;

int cbs_cli_fetch_service(CbsFetchService *service, char *error,
                          size_t error_size);
int cbs_cli_fetch_service_with_ca(CbsFetchService *service, char *error,
                                  size_t error_size, const char *ca_file);

typedef struct {
    /* Dependency role such as build, test, or runtime. */
    const char *role;
    /* Dependency kind such as tool, library, or compiler. */
    const char *kind;
    /* Declared dependency name. */
    const char *name;
} CbsDependency;

typedef struct {
    /* Caller-owned selected dependency array. */
    CbsDependency *items;
    /* Number of selected dependencies. */
    size_t count;
} CbsDependencySet;

typedef int (*CbsPhaseEvent)(const char *event, const char *phase, int status,
                             void *user);

typedef struct {
    /* Versioned event name: build-begin, phase-begin, command-begin, etc. */
    unsigned version;
    const char *type;
    unsigned long long sequence;
    /* Unix epoch timestamp in milliseconds, captured at emission time. */
    unsigned long long timestamp_ms;
    /* Correlation identifier shared by all events in one build. */
    const char *build_id;
    const char *package_name;
    const char *package_version;
    long package_release;
    const char *arch;
    const char *phase;
    const char *command;
    const char *arguments;
    /* Names of selected environment variables; values are never exposed. */
    const char *environment_names;
    const char *working_directory;
    const char *log_path;
    const char *message;
    const char *path;
    const char *rule;
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
    unsigned long long prune_bytes;
    unsigned long long prune_files;
} CbsBuildEvent;

typedef int (*CbsBuildEventSink)(const CbsBuildEvent *, void *user);

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
    unsigned long long prune_files;
    unsigned long long prune_bytes;
    int status;
    char artifact_path[4096];
    char failure_message[1024];
} CbsBuildReport;

typedef struct {
    /* Recipe path and source used for runtime diagnostics. */
    const char *recipe_path;
    const char *recipe_source;
    /* Canonical package fields copied from the recipe. */
    const char *name;
    const char *version;
    long release;
    const char *arch;
    /* Compiler selected by the package's structural compiler dependency. */
    const char *compiler;
    /* Confined source, build, and destination roots. */
    const char *src;
    const char *build;
    const char *dest;
    /* Caller-supplied, validated firmware tree for executor integrations. */
    const char *firmware_root;
    long jobs;
    /* Working directory used when launching child processes. */
    const char *working_directory;
    const char *log_directory;
    const char *current_log_path;
    const char *current_arguments;
    const char *current_environment_names;
    /* Verified source interpolation bindings. */
    const CbsNamedSource *sources;
    size_t source_count;
    const CbsEnvironmentBinding *environment;
    size_t environment_count;
    CbsOutputBinding *output_bindings;
    size_t output_binding_count;
    size_t output_binding_capacity;
    CbsGlobBinding *glob_bindings;
    size_t glob_binding_count;
    size_t glob_binding_capacity;
    CbsPhaseEvent phase_event;
    /* State passed to the phase-event callback. */
    void *phase_event_user;
    CbsBuildEventSink event_sink;
    /* State passed to the structured event sink. */
    void *event_sink_user;
    const char *build_id;
    const char *current_phase;
    const char *current_prune_path;
    const char *current_prune_rule;
    unsigned long long current_prune_bytes;
    unsigned long long event_sequence;
    unsigned long long current_cpu_ms;
    unsigned long long current_max_memory_bytes;
    unsigned long long current_source_bytes;
    unsigned long long current_fetch_duration_ms;
    unsigned long long current_tree_bytes;
    unsigned long long current_tree_files;
    unsigned long long current_artifact_bytes;
    struct {
        /* Maximum child address space in MiB; zero selects policy default. */
        long address_space_mb;
        /* Maximum child-created file size in MiB. */
        long file_size_mb;
        /* Maximum child CPU time in seconds. */
        long cpu_seconds;
        /* Maximum number of simultaneously open file descriptors. */
        long open_files;
        /* Maximum number of child processes. */
        long processes;
    } limits;
} CbsExecutionContext;

/* Allocate memory or terminate the process if the request cannot succeed. */
void *cbs_allocate(size_t size);
/* Resize an allocation while preserving its existing bytes. */
void *cbs_reallocate(void *pointer, size_t size);
/* Copy a NUL-terminated string into CBS-owned memory. */
char *cbs_duplicate(const char *text);
/* Copy a bounded character range and append a NUL terminator. */
char *cbs_duplicate_range(const char *start, size_t length);

/* Create, attach, and destroy nodes in the CPDL abstract syntax tree. */
CbsNode *cbs_node_create(CbsNodeKind kind, CbsLocation location);
void cbs_node_add(CbsNode *parent, CbsNode *child);
void cbs_node_destroy(CbsNode *node);

/* Release a token list after lexing and parsing are complete. */
void cbs_token_list_destroy(CbsTokenList *tokens);
/* Convert source text into validated lexical tokens. */
int cbs_lex(const char *path, const char *source, size_t length,
            CbsTokenList *tokens);
/* Convert tokens into a CPDL abstract syntax tree. */
CbsNode *cbs_parse(const char *path, const char *source, size_t length,
                   CbsTokenList *tokens);
/* Check the AST against CPDL language and policy rules. */
int cbs_validate(const CbsNode *document, const char *path, const char *source);
/* Reject commands CBS must never execute from a recipe. */
int cbs_is_forbidden_executable(const char *value);
/* Reject compilers that violate the default toolchain policy. */
int cbs_is_forbidden_compiler(const char *value);
/* Resolve a CPDL interpolation against the execution context. */
char *cbs_resolve_value(const char *value, int token_kind,
                        const CbsExecutionContext *context);
/* Execute one process operation. */
int cbs_execute_run(const CbsNode *run, const CbsExecutionContext *context);
/* Execute one confined filesystem operation. */
int cbs_execute_filesystem(const CbsNode *operation,
                           const CbsExecutionContext *context);
/* Copy one named source into the build workspace. */
int cbs_execute_materialize(const CbsNode *operation,
                            const CbsExecutionContext *context);
/* Apply a source edit and assert its expected match cardinality. */
int cbs_execute_edit_assertion(const CbsNode *operation,
                               const CbsExecutionContext *context);
int cbs_execute_glob_binding(const CbsNode *operation,
                             const CbsExecutionContext *context);
int cbs_execute_links(const CbsNode *operation,
                      const CbsExecutionContext *context);
int cbs_execute_patch(const CbsNode *operation,
                      const CbsExecutionContext *context);
/* Resolve and validate a path beneath an execution root. */
char *cbs_resolve_confined_path(const char *logical,
                                const CbsExecutionContext *context);
/* Execute every operation in one phase block. */
int cbs_execute_block(const CbsNode *block, const CbsExecutionContext *context);
/* Apply the shared job ceiling to a requested parallelism value. */
long cbs_effective_jobs(long requested, long cpu_budget,
                        long administrator_limit);
typedef struct {
    /* Whether absolute paths are rejected by stage validation. */
    int reject_absolute;
    /* Whether parent-directory traversal is rejected. */
    int reject_parent;
    /* Whether an empty path is rejected. */
    int reject_empty;
} CbsStagePolicy;
int cbs_validate_stage_path(const char *path, const CbsStagePolicy *policy);
typedef struct {
    /* Canonical path relative to the staged package root. */
    const char *path;
    /* Entry kind, such as a regular file, directory, or symlink. */
    char type;
    /* Permission bits preserved in the package manifest. */
    unsigned mode;
    /* Original owner user ID observed during staging. */
    unsigned uid;
    /* Original owner group ID observed during staging. */
    unsigned gid;
    /* Regular-file length in bytes. */
    unsigned long long size;
    /* SHA-256 digest for a regular file, when applicable. */
    const char *digest;
    /* Symlink destination, when this entry is a symbolic link. */
    const char *target;
} CbsManifestEntry;
/* Compare manifest entries by their canonical path. */
int cbs_manifest_compare(const void *left, const void *right);
/* Collect and sort every supported entry beneath a staged root. */
int cbs_manifest_collect(const char *root, CbsManifestEntry **entries,
                         size_t *count);
int cbs_manifest_collect_with_error(const char *root,
                                    CbsManifestEntry **entries,
                                    size_t *count, char *error,
                                    size_t error_size);
/* Release a manifest-entry array and its owned strings. */
void cbs_manifest_entries_destroy(CbsManifestEntry *entries, size_t count);
/* Write a deterministic typed manifest for a staged root. */
int cbs_manifest_write(const char *root, const char *output);
int cbs_manifest_write_with_license(const char *root, const char *output,
                                    const char *license);
int cbs_manifest_write_with_license_error(const char *root,
                                          const char *output,
                                          const char *license, char *error,
                                          size_t error_size);
/* Build a package from an already staged tree. */
int cbs_build_package(const char *recipe, const char *staged_root,
                      const char *package_path);
/* Build a standalone package using the default source cache behavior. */
int cbs_build_standalone(const char *recipe, const char *workspace,
                         const char *package_path, const char *architecture,
                         const CbsFetchService *fetch_service);
/* Build a standalone package while using an explicit source cache. */
int cbs_build_standalone_with_cache(const char *recipe, const char *workspace,
                                    const char *package_path,
                                    const char *architecture,
                                    const CbsFetchService *fetch_service,
                                    const char *cache_directory);
typedef int (*CbsFinalizePolicy)(const char *staged_root, void *user);
typedef struct {
    int strip_debug;
    int drop_static_archives;
    int drop_libtool_archives;
} CbsPrunePolicy;
int cbs_prune_policy_load(const char *path, CbsPrunePolicy *policy,
                          char *error, size_t error_size);
int cbs_prune_staged_tree(const char *root, const CbsPrunePolicy *policy,
                          CbsExecutionContext *context);
/* Build a package and run an embedder policy before manifest generation. */
int cbs_build_standalone_with_cache_policy(
    const char *recipe, const char *workspace, const char *package_path,
    const char *architecture, const CbsFetchService *fetch_service,
    const char *cache_directory, CbsFinalizePolicy finalize, void *user);
int cbs_build_standalone_with_events(
    const char *recipe, const char *workspace, const char *package_path,
    const char *architecture, const CbsFetchService *fetch_service,
    const char *cache_directory, CbsFinalizePolicy finalize, void *user,
    CbsBuildEventSink event_sink, void *event_sink_user);
int cbs_build_standalone_with_events_policy(
    const char *recipe, const char *workspace, const char *package_path,
    const char *architecture, const CbsFetchService *fetch_service,
    const char *cache_directory, CbsFinalizePolicy finalize, void *user,
    const char *firmware_root, const CbsPrunePolicy *prune_policy,
    CbsBuildEventSink event_sink,
    void *event_sink_user);
#define CBS_MAX_PHASES 5
typedef struct {
    /* AST nodes for phases in their declared execution order. */
    const CbsNode *phases[CBS_MAX_PHASES];
    /* Number of initialized phase pointers. */
    size_t count;
} CbsBuildPlan;
typedef struct {
    /* Build image selected for the recipe, if one was declared. */
    const char *build_image;
    /* Upstream discovery source selected by the recipe. */
    const char *upstream;
    /* Toolchain policy selected by the recipe. */
    const char *toolchain;
    /* Human-readable reason for the selected toolchain policy. */
    const char *toolchain_reason;
    /* Capability names declared by the recipe. */
    const char **capabilities;
    /* Number of initialized capability names. */
    size_t capability_count;
    /* SPDX-style package license declaration, when present. */
    const char *license;
} CbsBuildMetadata;
int cbs_emit_build_event(const CbsExecutionContext *context, const char *type,
                         const char *phase, const char *command,
                         const char *message, int status, long duration_ms,
                         unsigned long long stdout_bytes,
                         unsigned long long stderr_bytes);
int cbs_build_event_jsonl(const CbsBuildEvent *event, void *user);
int cbs_build_event_human(const CbsBuildEvent *event, void *user);
void cbs_build_report_init(CbsBuildReport *report);
int cbs_build_report_consume(const CbsBuildEvent *event, void *user);
int cbs_build_report_write_json(const CbsBuildReport *report, FILE *stream);
/* Convert a validated package AST into its ordered phase plan. */
int cbs_build_plan(const CbsNode *document, CbsBuildPlan *plan);
/* Extract build-image, toolchain, and capability metadata. */
int cbs_build_metadata(const CbsNode *document, CbsBuildMetadata *metadata);
/* Execute an ordered phase plan and report phase events. */
int cbs_execute_plan(const CbsBuildPlan *plan,
                     const CbsExecutionContext *context);
/* Create the source, build, destination, cache, and temporary directories. */
int cbs_workspace_prepare(const char *root);
typedef int (*CbsDaemonRequest)(const char *operation, const char *payload,
                                char *response, size_t response_size,
                                void *user);
/* Forward one request through an embedder-provided daemon adapter. */
int cbs_daemon_request(CbsDaemonRequest request, void *user,
                       const char *operation, const char *payload,
                       char *response, size_t response_size);
typedef int (*CbsSandboxHook)(const char *root, void *user);
/* Enter, run in, and leave an embedder-provided sandbox. */
int cbs_sandbox_run(CbsSandboxHook enter, CbsSandboxHook leave,
                    const char *root, void *user);
typedef int (*CbsHealthCheck)(void *user);
/* Ask an embedder-provided service health check whether it is ready. */
int cbs_service_health(CbsHealthCheck check, void *user);
typedef int (*CbsDependencyObserver)(const char *path, void *user);
/* Inspect an ELF file and report its declared shared-library dependencies. */
int cbs_observe_dependencies(CbsDependencyObserver observer, const char *path,
                             void *user);
typedef int (*CbsSignatureVerifier)(const unsigned char *data, size_t length,
                                    void *user);
/* Verify a file through an embedder-provided detached-signature adapter. */
int cbs_verify_signature(CbsSignatureVerifier verifier, const char *path,
                         void *user);
/* Compress a standalone blob with the CIXPKG zstd policy. */
int cbs_cixpkg_compress(const char *input, const char *output);
/* Decompress a CIXPKG blob after validating its bounded frame size. */
int cbs_cixpkg_decompress(const char *input, const char *output);
/* Write one typed CIXPKG v2 artifact from a manifest and staged root. */
int cbs_cixpkg_write_tree(const char *manifest, const char *root,
                          const char *package_path, const char *identity);
#define CBS_CIXPKG_FLAG_FINALIZED 1U
/* Write a CIXPKG v2 artifact and set approved metadata flags. */
int cbs_cixpkg_write_tree_with_flags(const char *manifest, const char *root,
                                     const char *package_path,
                                     const char *identity, unsigned flags);
/* Verify headers, digests, paths, metadata, and payload contents. */
int cbs_cixpkg_verify_tree(const char *package_path, char *identity,
                           size_t identity_size);
int cbs_cixpkg_read_license(const char *package_path, char *license,
                            size_t license_size);
/* Verify and atomically extract a CIXPKG artifact into a new directory. */
int cbs_cixpkg_extract(const char *package_path, const char *destination);
/* Atomically rename a staged file after applying its final mode. */
int cbs_install_atomic(const char *staged, const char *destination,
                       unsigned mode);
/* Compare two files byte-for-byte. */
int cbs_compare_files(const char *left, const char *right);
/* Merge a base and fragment kconfig using only curated CONFIG_* states. */
int cbs_kconfig_merge(const char *base, const char *fragment,
                      const char *output, char *error, size_t error_size);
/* Read the canonical package identity from a validated document. */
int cbs_identity_from_document(const CbsNode *document,
                               const char *architecture,
                               CbsPackageIdentity *identity);
/* Format an identity as name-version-release-architecture. */
char *cbs_identity_string(const CbsPackageIdentity *identity);
/* Format the canonical artifact filename for an identity. */
char *cbs_identity_artifact_filename(const CbsPackageIdentity *identity);
/* Build the stable metadata bytes used as an identity digest input. */
char *cbs_identity_digest_metadata(const CbsPackageIdentity *identity,
                                   size_t *length);
/* Copy identity fields into an execution context. */
void cbs_identity_apply_execution_context(const CbsPackageIdentity *identity,
                                          CbsExecutionContext *context);
/* Read named source declarations from the package AST. */
int cbs_sources_from_document(const CbsNode *document, CbsSourceSet *sources);
/* Release source declarations and their cache bindings. */
void cbs_source_set_destroy(CbsSourceSet *sources);
/* Verify one source file against its declared digest. */
int cbs_source_verify(CbsSource *source, const char *path,
                      const char *recipe_path, const char *recipe_source,
                      CbsLocation location);
/* Bind verified source paths to interpolation names in an execution context. */
int cbs_sources_apply_execution_context(CbsSourceSet *sources,
                                        CbsExecutionContext *context);
/* Fetch or load all declared sources using the cache-first policy. */
int cbs_sources_fetch(CbsSourceSet *sources, const char *cache_directory,
                      const CbsFetchService *service, const char *recipe_path,
                      const char *recipe_source, CbsLocation location);
/* Extract one verified archive while rejecting unsafe entries. */
int cbs_extract_archive(const char *archive_path, const char *destination,
                        const char *source_name, const char *recipe_path,
                        const char *recipe_source, CbsLocation location);
/* Probe whether a verified file is a supported archive (1), an ordinary
 * non-archive file (0), or a recognized but invalid/unsupported archive (-1). */
int cbs_archive_probe(const char *archive_path);
/* Fetch and extract all sources into the build source directory. */
int cbs_prepare_sources(CbsSourceSet *sources, const char *cache_directory,
                        const char *source_root, const CbsFetchService *service,
                        const char *recipe_path, const char *recipe_source,
                        CbsLocation location);
int cbs_prepare_sources_with_events(
    CbsSourceSet *sources, const char *cache_directory, const char *source_root,
    const CbsFetchService *service, const char *recipe_path,
    const char *recipe_source, CbsLocation location,
    const CbsExecutionContext *context);
/* Compute a lowercase SHA-256 digest for a file. */
int cbs_digest_file(const char *path, char output[65]);
/* Compute a lowercase SHA-256 digest for a byte string. */
int cbs_digest_text(const char *text, size_t length, char output[65]);
/* Collect dependencies selected by one named phase. */
int cbs_dependencies_for_phase(const CbsNode *document, const char *phase,
                               CbsDependencySet *dependencies);
/* Release a dependency set returned by the phase selector. */
void cbs_dependency_set_destroy(CbsDependencySet *dependencies);
/* Test whether a dependency kind/name pair is present. */
int cbs_dependency_set_contains(const CbsDependencySet *dependencies,
                                const char *kind, const char *name);

/* Emit one located human-readable or JSON diagnostic. */
void cbs_diagnostic(const char *path, const char *source, CbsLocation location,
                    const char *severity, const char *code,
                    CbsDiagCategory category, const char *message);
/* Select JSON diagnostics when enabled is nonzero. */
void cbs_diagnostic_set_json(int enabled);
/* Emit the expected-value detail associated with a failed assertion. */
void cbs_diagnostic_expected(const char *path, const char *source,
                             CbsLocation location, const char *expected,
                             const char *found);

#endif
