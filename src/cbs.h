#ifndef CBS_H
#define CBS_H

#include <stddef.h>

typedef struct {
    const char *path;
    size_t line;
    size_t column;
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
    CbsTokenKind kind;
    char *text;
    CbsLocation location;
} CbsToken;

typedef struct {
    CbsToken *items;
    size_t count;
    size_t capacity;
} CbsTokenList;

typedef enum {
    CBS_NODE_DOCUMENT,
    CBS_NODE_PACKAGE,
    CBS_NODE_VERSION,
    CBS_NODE_RELEASE,
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
    CBS_NODE_REQUIRE,
    CBS_NODE_ON_FAIL,
    CBS_NODE_PROPERTY,
    CBS_NODE_BUILD_IMAGE,
    CBS_NODE_CAPABILITY,
    CBS_NODE_TOOLCHAIN,
    CBS_NODE_UPSTREAM
} CbsNodeKind;

typedef struct CbsNode CbsNode;

struct CbsNode {
    CbsNodeKind kind;
    CbsLocation location;
    char *name;
    char *value;
    char *second_value;
    long number;
    int flag;
    int second_flag;
    CbsNode **children;
    size_t child_count;
    size_t child_capacity;
};

typedef struct {
    const char *path;
    const char *source;
    size_t source_length;
    CbsTokenList tokens;
    size_t cursor;
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
    const char *name;
    const char *path;
} CbsNamedSource;

typedef struct {
    const char *name;
    const char *value;
} CbsEnvironmentBinding;

typedef struct {
    const char *name;
    const char *version;
    long release;
    const char *architecture;
} CbsPackageIdentity;

typedef struct {
    const char *kind;
    const char *name;
    const char **urls;
    size_t url_count;
    const char *sha256;
    char *verified_path;
} CbsSource;

typedef struct {
    CbsSource *items;
    size_t count;
    CbsNamedSource *bindings;
} CbsSourceSet;

typedef int (*CbsFetchFunction)(const char *url, const char *destination,
                                void *user, char *error, size_t error_size);

typedef struct {
    CbsFetchFunction fetch;
    void *user;
} CbsFetchService;

int cbs_cli_fetch_service(CbsFetchService *service, char *error,
                          size_t error_size);
int cbs_cli_fetch_service_with_ca(CbsFetchService *service, char *error,
                                  size_t error_size, const char *ca_file);

typedef struct {
    const char *role;
    const char *kind;
    const char *name;
} CbsDependency;

typedef struct {
    CbsDependency *items;
    size_t count;
} CbsDependencySet;

typedef struct {
    const char *recipe_path;
    const char *recipe_source;
    const char *name;
    const char *version;
    long release;
    const char *arch;
    const char *src;
    const char *build;
    const char *dest;
    long jobs;
    const char *working_directory;
    const CbsNamedSource *sources;
    size_t source_count;
    const CbsEnvironmentBinding *environment;
    size_t environment_count;
    struct {
        long address_space_mb;
        long file_size_mb;
        long cpu_seconds;
        long open_files;
        long processes;
    } limits;
} CbsExecutionContext;

void *cbs_allocate(size_t size);
void *cbs_reallocate(void *pointer, size_t size);
char *cbs_duplicate(const char *text);
char *cbs_duplicate_range(const char *start, size_t length);

CbsNode *cbs_node_create(CbsNodeKind kind, CbsLocation location);
void cbs_node_add(CbsNode *parent, CbsNode *child);
void cbs_node_destroy(CbsNode *node);

void cbs_token_list_destroy(CbsTokenList *tokens);
int cbs_lex(const char *path, const char *source, size_t length,
            CbsTokenList *tokens);
CbsNode *cbs_parse(const char *path, const char *source, size_t length,
                   CbsTokenList *tokens);
int cbs_validate(const CbsNode *document, const char *path,
                 const char *source);
int cbs_is_forbidden_executable(const char *value);
int cbs_is_forbidden_compiler(const char *value);
char *cbs_resolve_value(const char *value, int token_kind,
                        const CbsExecutionContext *context);
int cbs_execute_run(const CbsNode *run, const CbsExecutionContext *context);
int cbs_execute_filesystem(const CbsNode *operation,
                           const CbsExecutionContext *context);
int cbs_execute_materialize(const CbsNode *operation,
                            const CbsExecutionContext *context);
int cbs_execute_edit_assertion(const CbsNode *operation,
                               const CbsExecutionContext *context);
char *cbs_resolve_confined_path(const char *logical,
                                const CbsExecutionContext *context);
int cbs_execute_block(const CbsNode *block,
                      const CbsExecutionContext *context);
long cbs_effective_jobs(long requested, long cpu_budget, long administrator_limit);
typedef struct { int reject_absolute; int reject_parent; int reject_empty; } CbsStagePolicy;
int cbs_validate_stage_path(const char *path, const CbsStagePolicy *policy);
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
int cbs_manifest_compare(const void *left, const void *right);
int cbs_manifest_collect(const char *root, CbsManifestEntry **entries,
                         size_t *count);
void cbs_manifest_entries_destroy(CbsManifestEntry *entries, size_t count);
int cbs_manifest_write(const char *root, const char *output);
int cbs_build_package(const char *recipe, const char *staged_root,
                      const char *package_path);
int cbs_build_standalone(const char *recipe, const char *workspace,
                         const char *package_path, const char *architecture,
                         const CbsFetchService *fetch_service);
int cbs_build_standalone_with_cache(const char *recipe, const char *workspace,
                                    const char *package_path,
                                    const char *architecture,
                                    const CbsFetchService *fetch_service,
                                    const char *cache_directory);
typedef int (*CbsFinalizePolicy)(const char *staged_root, void *user);
int cbs_build_standalone_with_cache_policy(
    const char *recipe, const char *workspace, const char *package_path,
    const char *architecture, const CbsFetchService *fetch_service,
    const char *cache_directory, CbsFinalizePolicy finalize, void *user);
#define CBS_MAX_PHASES 5
typedef struct { const CbsNode *phases[CBS_MAX_PHASES]; size_t count; } CbsBuildPlan;
typedef struct {
    const char *build_image;
    const char *upstream;
    const char *toolchain;
    const char *toolchain_reason;
    const char **capabilities;
    size_t capability_count;
} CbsBuildMetadata;
int cbs_build_plan(const CbsNode *document, CbsBuildPlan *plan);
int cbs_build_metadata(const CbsNode *document, CbsBuildMetadata *metadata);
int cbs_execute_plan(const CbsBuildPlan *plan, const CbsExecutionContext *context);
int cbs_workspace_prepare(const char *root);
typedef int (*CbsDaemonRequest)(const char *operation, const char *payload,
                                char *response, size_t response_size,
                                void *user);
int cbs_daemon_request(CbsDaemonRequest request, void *user,
                       const char *operation, const char *payload,
                       char *response, size_t response_size);
typedef int (*CbsSandboxHook)(const char *root, void *user);
int cbs_sandbox_run(CbsSandboxHook enter, CbsSandboxHook leave,
                    const char *root, void *user);
typedef int (*CbsHealthCheck)(void *user);
int cbs_service_health(CbsHealthCheck check, void *user);
typedef int (*CbsDependencyObserver)(const char *path, void *user);
int cbs_observe_dependencies(CbsDependencyObserver observer, const char *path,
                             void *user);
typedef int (*CbsSignatureVerifier)(const unsigned char *data, size_t length,
                                    void *user);
int cbs_verify_signature(CbsSignatureVerifier verifier, const char *path,
                         void *user);
int cbs_cixpkg_compress(const char *input, const char *output);
int cbs_cixpkg_decompress(const char *input, const char *output);
int cbs_cixpkg_write_tree(const char *manifest, const char *root,
                          const char *package_path, const char *identity);
#define CBS_CIXPKG_FLAG_FINALIZED 1U
int cbs_cixpkg_write_tree_with_flags(const char *manifest, const char *root,
                                     const char *package_path,
                                     const char *identity, unsigned flags);
int cbs_cixpkg_verify_tree(const char *package_path, char *identity,
                           size_t identity_size);
int cbs_cixpkg_extract(const char *package_path, const char *destination);
int cbs_install_atomic(const char *staged, const char *destination, unsigned mode);
int cbs_compare_files(const char *left, const char *right);
int cbs_identity_from_document(const CbsNode *document,
                               const char *architecture,
                               CbsPackageIdentity *identity);
char *cbs_identity_string(const CbsPackageIdentity *identity);
char *cbs_identity_artifact_filename(const CbsPackageIdentity *identity);
char *cbs_identity_digest_metadata(const CbsPackageIdentity *identity,
                                   size_t *length);
void cbs_identity_apply_execution_context(const CbsPackageIdentity *identity,
                                          CbsExecutionContext *context);
int cbs_sources_from_document(const CbsNode *document, CbsSourceSet *sources);
void cbs_source_set_destroy(CbsSourceSet *sources);
int cbs_source_verify(CbsSource *source, const char *path,
                      const char *recipe_path, const char *recipe_source,
                      CbsLocation location);
int cbs_sources_apply_execution_context(CbsSourceSet *sources,
                                        CbsExecutionContext *context);
int cbs_sources_fetch(CbsSourceSet *sources, const char *cache_directory,
                      const CbsFetchService *service,
                      const char *recipe_path, const char *recipe_source,
                      CbsLocation location);
int cbs_extract_archive(const char *archive_path, const char *destination,
                        const char *source_name, const char *recipe_path,
                      const char *recipe_source, CbsLocation location);
int cbs_prepare_sources(CbsSourceSet *sources, const char *cache_directory,
                        const char *source_root, const CbsFetchService *service,
                        const char *recipe_path, const char *recipe_source,
                        CbsLocation location);
int cbs_digest_file(const char *path, char output[65]);
int cbs_digest_text(const char *text, size_t length, char output[65]);
int cbs_dependencies_for_phase(const CbsNode *document, const char *phase,
                               CbsDependencySet *dependencies);
void cbs_dependency_set_destroy(CbsDependencySet *dependencies);
int cbs_dependency_set_contains(const CbsDependencySet *dependencies,
                                const char *kind, const char *name);

void cbs_diagnostic(const char *path, const char *source,
                    CbsLocation location, const char *severity,
                    const char *code, CbsDiagCategory category,
                    const char *message);
void cbs_diagnostic_set_json(int enabled);
void cbs_diagnostic_expected(const char *path, const char *source,
                             CbsLocation location, const char *expected,
                             const char *found);

#endif
