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
    CBS_NODE_REPLACE,
    CBS_NODE_INSERT,
    CBS_NODE_REQUIRE,
    CBS_NODE_ON_FAIL,
    CBS_NODE_PROPERTY
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
    CBS_DIAG_INTERNAL
} CbsDiagCategory;

typedef struct {
    const char *name;
    const char *path;
} CbsNamedSource;

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
char *cbs_resolve_value(const char *value, int token_kind,
                        const CbsExecutionContext *context);
int cbs_execute_run(const CbsNode *run, const CbsExecutionContext *context);
int cbs_execute_filesystem(const CbsNode *operation,
                           const CbsExecutionContext *context);
int cbs_execute_edit_assertion(const CbsNode *operation,
                               const CbsExecutionContext *context);

void cbs_diagnostic(const char *path, const char *source,
                    CbsLocation location, const char *severity,
                    const char *code, CbsDiagCategory category,
                    const char *message);
void cbs_diagnostic_expected(const char *path, const char *source,
                             CbsLocation location, const char *expected,
                             const char *found);

#endif
