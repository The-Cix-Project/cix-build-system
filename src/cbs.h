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
#include "cbs_public.h"

/* Select the same UTF-8 LC_CTYPE fallback used by archive processing. */
const char *cbs_select_utf8_locale(void);

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
    CBS_NODE_REPLACES,
    CBS_NODE_DEPENDENCY_GROUP,
    CBS_NODE_DEPENDENCY,
    CBS_NODE_PHASE,
    CBS_NODE_RUN,
    CBS_NODE_ARGUMENT,
    CBS_NODE_RUN_ENV,
    CBS_NODE_RUN_JOBS,
    CBS_NODE_RUN_TIMEOUT,
    CBS_NODE_RUN_EXPECT,
    CBS_NODE_RUN_GLOB,
    CBS_NODE_RUN_INPUT,
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
    CBS_NODE_CASE,
    CBS_NODE_ON_FAIL,
    CBS_NODE_PROPERTY,
    CBS_NODE_BUILD_IMAGE,
    CBS_NODE_CAPABILITY,
    CBS_NODE_TOOLCHAIN,
    CBS_NODE_UPSTREAM,
    CBS_NODE_METADATA,
    CBS_NODE_RESOURCES,
    /* CBS-owned compiler/tool adaptation declarations. */
    CBS_NODE_TOOLS,
    CBS_NODE_TOOL,
    /* stage library/file/tree from an approved build image root. */
    CBS_NODE_STAGE,
    /* Explicit package-level authorization for one privileged file mode. */
    CBS_NODE_PRIVILEGED
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
    /* Whether a trailing allow_failure permits this operation to fail.  It is
     * its own field because `flag` and `second_flag` carry the token kinds of
     * `value` and `second_value`, and a `write` whose text is a quoted string
     * therefore has a nonzero `second_flag` that means nothing of the sort. */
    int allow_failure;
    /* Whether a source-edit target is a glob selector. */
    int selector_glob;
    /* Whether an insert operation writes before, rather than after, a match. */
    int insert_before;
    /* Whether a run output option targets stderr rather than stdout. */
    int stderr_stream;
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

/* Allocate memory or terminate the process if the request cannot succeed. */
void *cbs_allocate(size_t size);
/* Resize an allocation while preserving its existing bytes. */
void *cbs_reallocate(void *pointer, size_t size);
/* Copy a NUL-terminated string into CBS-owned memory. */
char *cbs_duplicate(const char *text);
/* Copy a bounded character range and append a NUL terminator. */
char *cbs_duplicate_range(const char *start, size_t length);
/* Validate a colon-separated command search policy. */
int cbs_command_path_is_valid(const char *command_path);
/* Validate a colon-separated library-root policy. */
int cbs_library_path_is_valid(const char *library_path);
/* Resolve one bare command using the approved command search path. */
char *cbs_resolve_executable(const char *program,
                             const char *working_directory,
                             const char *command_path);
char *cbs_resolve_stage_source(const char *source, const char *command_path,
                               const char *library_path, int want_tree,
                               char *searched, size_t searched_size);

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
/* Expand a confined glob into sorted, owned argument paths. */
int cbs_expand_glob(const char *pattern, const CbsExecutionContext *context,
                    char ***matches, size_t *count);
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
    /* Exact permission bits authorized for this path. */
    unsigned mode;
} CbsPrivilegedAllowance;
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
int cbs_manifest_write_with_license_policy_error(
    const char *root, const char *output, const char *license,
    const CbsPrivilegedAllowance *allowances, size_t allowance_count,
    char *error, size_t error_size);
int cbs_manifest_collect_with_policy_error(
    const char *root, CbsManifestEntry **entries, size_t *count,
    const CbsPrivilegedAllowance *allowances, size_t allowance_count,
    char *error, size_t error_size);
/* Build a package from an already staged tree. */
int cbs_build_package(const char *recipe, const char *staged_root,
                      const char *package_path);
/* Emit a CIXPKG directly from a caller-assembled staged tree. */
int cbs_package_staged_tree(const char *staged_root,
                            const CbsPackageIdentity *identity,
                            const char *license, const char *package_path);
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
int cbs_build_standalone_with_events_policy_path(
    const char *recipe, const char *workspace, const char *package_path,
    const char *architecture, const CbsFetchService *fetch_service,
    const char *cache_directory, CbsFinalizePolicy finalize, void *user,
    const char *firmware_root, const CbsPrunePolicy *prune_policy,
    const char *command_path, const char *library_path,
    CbsBuildEventSink event_sink,
    void *event_sink_user);
int cbs_build_standalone_with_events_policy_path_inputs(
    const char *recipe, const char *workspace, const char *package_path,
    const char *architecture, const CbsFetchService *fetch_service,
    const char *cache_directory, CbsFinalizePolicy finalize, void *user,
    const char *firmware_root, const CbsPrunePolicy *prune_policy,
    const char *command_path, const char *library_path,
    const CbsInputBinding *inputs, size_t input_count,
    CbsBuildEventSink event_sink, void *event_sink_user);
int cbs_build_standalone_with_events_policy_path_inputs_tool_identities(
    const char *, const char *, const char *, const char *,
    const CbsFetchService *, const char *, CbsFinalizePolicy, void *,
    const char *, const CbsPrunePolicy *, const char *, const char *,
    const CbsInputBinding *, size_t, const CbsToolIdentity *, size_t,
    CbsBuildEventSink, void *);
int cbs_build_standalone_with_events_policy_path_inputs_tool_identities_result(
    const char *, const char *, const char *, const char *,
    const CbsFetchService *, const char *, CbsFinalizePolicy, void *,
    const char *, const CbsPrunePolicy *, const char *, const char *,
    const CbsInputBinding *, size_t, const CbsToolIdentity *, size_t,
    CbsBuildEventSink, void *, char[65]);
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
    /* Aggregate build memory need in bytes, when declared. */
    unsigned long long memory_bytes;
    int memory_declared;
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
/* Inspect an ELF file and report its declared shared-library dependencies. */
int cbs_observe_dependencies(CbsDependencyObserver observer, const char *path,
                             void *user);
const char *cbs_version(void);
unsigned cbs_api_version(void);
unsigned cbs_abi_version(void);
size_t cbs_execution_context_size(void);
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
/* List a verified CIXPKG manifest without extracting it. */
int cbs_cixpkg_list(const char *package_path, FILE *stream, int json);
int cbs_cixpkg_list_filtered(const char *package_path, FILE *stream, int json,
                             const char *prefix, char type);
/* Diff two verified CIXPKG manifests; different is set when they differ. */
int cbs_cixpkg_diff(const char *left, const char *right, FILE *stream,
                    int json, int *different);
int cbs_cixpkg_diff_filtered(const char *left, const char *right, FILE *stream,
                             int json, int *different, const char *prefix,
                             char type);
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
/* Extract only the named archive members, with the same safety policy. */
int cbs_extract_archive_members(
    const char *archive_path, const char *destination,
    const char *const *members, size_t member_count, const char *source_name,
    const char *recipe_path, const char *recipe_source, CbsLocation location);
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
/* Compute the deterministic material-input fingerprint for one build action. */
int cbs_build_fingerprint(const char *recipe, const char *architecture,
                          const char *command_path, const char *library_path,
                          const CbsInputBinding *inputs, size_t input_count,
                          char output[65]);
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
/* Set the CLI verb carried by the versioned diagnostic envelope. */
void cbs_diagnostic_set_verb(const char *verb);
/* Return nonzero when machine-readable diagnostics are enabled. */
int cbs_diagnostic_is_json(void);
/* Emit one non-located CLI diagnostic in the versioned JSON envelope. */
void cbs_cli_diagnostic(const char *severity, const char *code,
                        const char *category, const char *message,
                        const char *subject, int status);
/* Emit the expected-value detail associated with a failed assertion. */
void cbs_diagnostic_expected(const char *path, const char *source,
                             CbsLocation location, const char *expected,
                             const char *found);

#endif
