/* Recursive-descent parser that turns CPDL tokens into an AST. */
#include "cbs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Return the token currently under the parser cursor. */
static CbsToken *current(CbsParser *parser) {
    return &parser->tokens.items[parser->cursor];
}

/* Test whether the current token is a specific word. */
static int is_word(CbsParser *parser, const char *word) {
    CbsToken *token = current(parser);
    return token->kind == CBS_TOKEN_WORD && strcmp(token->text, word) == 0;
}

/* Consume and return the current token. */
static CbsToken *advance(CbsParser *parser) {
    CbsToken *token = current(parser);
    if (token->kind != CBS_TOKEN_EOF)
        ++parser->cursor;
    return token;
}

/* Produce a readable description for an unexpected token. */
static const char *token_description(const CbsToken *token) {
    switch (token->kind) {
    case CBS_TOKEN_EOF:
        return "end of file";
    case CBS_TOKEN_LBRACE:
        return "{";
    case CBS_TOKEN_RBRACE:
        return "}";
    case CBS_TOKEN_EQUAL:
        return "=";
    default:
        return token->text;
    }
}

/* Report a missing grammar element once and mark parsing failed. */
static void expected(CbsParser *parser, const char *description) {
    CbsToken *token = current(parser);
    if (!parser->failed)
        cbs_diagnostic_expected(parser->path, parser->source, token->location,
                                description, token_description(token));
    parser->failed = 1;
}

static CbsToken *consume_kind(CbsParser *parser, CbsTokenKind kind,
                              const char *description) {
    if (current(parser)->kind != kind) {
        expected(parser, description);
        return NULL;
    }
    return advance(parser);
}

/* Consume a required keyword or emit its expected-token diagnostic. */
static CbsToken *consume_word(CbsParser *parser, const char *word) {
    if (!is_word(parser, word)) {
        expected(parser, word);
        return NULL;
    }
    return advance(parser);
}

/* Identify tokens accepted wherever CPDL expects a string value. */
static int is_text_value(const CbsToken *token) {
    return token->kind == CBS_TOKEN_STRING ||
           token->kind == CBS_TOKEN_BLOCK_STRING ||
           token->kind == CBS_TOKEN_CBS_VALUE;
}

static CbsToken *consume_text_value(CbsParser *parser,
                                    const char *description) {
    if (!is_text_value(current(parser))) {
        expected(parser, description);
        return NULL;
    }
    return advance(parser);
}

/* Create a string-valued AST node from one source token. */
static CbsNode *node_from_token(CbsNodeKind kind, const CbsToken *token) {
    CbsNode *node = cbs_node_create(kind, token->location);
    node->value = cbs_duplicate(token->text);
    node->flag = token->kind;
    return node;
}

static CbsNode *parse_operation_block(CbsParser *parser, CbsNode *owner,
                                      int diagnostic_only);

/* Parse a process operation and its arguments/options. */
static CbsNode *parse_run(CbsParser *parser, int diagnostic_only) {
    CbsToken *keyword = consume_word(parser, "run");
    CbsToken *program;
    CbsNode *node;

    if (keyword == NULL)
        return NULL;
    node = cbs_node_create(CBS_NODE_RUN, keyword->location);
    program = consume_text_value(parser, "executable value");
    if (program == NULL)
        return node;
    node->value = cbs_duplicate(program->text);
    node->flag = program->kind;
    if (consume_kind(parser, CBS_TOKEN_LBRACE, "{") == NULL)
        return node;
    while (!parser->failed && current(parser)->kind != CBS_TOKEN_RBRACE &&
           current(parser)->kind != CBS_TOKEN_EOF) {
        CbsToken *token = current(parser);
        CbsNode *item = NULL;

        if (is_text_value(token)) {
            advance(parser);
            item = node_from_token(CBS_NODE_ARGUMENT, token);
            item->flag = token->kind;
        } else if (is_word(parser, "env")) {
            CbsToken *name;
            CbsToken *value;
            advance(parser);
            item = cbs_node_create(CBS_NODE_RUN_ENV, token->location);
            name = consume_kind(parser, CBS_TOKEN_STRING,
                                "environment name string");
            if (consume_kind(parser, CBS_TOKEN_EQUAL, "=") == NULL)
                break;
            value = consume_text_value(parser, "environment value");
            if (name != NULL)
                item->name = cbs_duplicate(name->text);
            if (value != NULL) {
                item->value = cbs_duplicate(value->text);
                item->flag = value->kind;
            }
        } else if (is_word(parser, "jobs")) {
            advance(parser);
            item = cbs_node_create(CBS_NODE_RUN_JOBS, token->location);
            if (current(parser)->kind == CBS_TOKEN_INTEGER) {
                item->number = strtol(advance(parser)->text, NULL, 10);
            } else if (current(parser)->kind == CBS_TOKEN_CBS_VALUE) {
                item->value = cbs_duplicate(advance(parser)->text);
            } else {
                expected(parser, "integer or $jobs");
            }
        } else if (is_word(parser, "timeout")) {
            CbsToken *duration;
            advance(parser);
            item = cbs_node_create(CBS_NODE_RUN_TIMEOUT, token->location);
            duration = consume_kind(parser, CBS_TOKEN_DURATION, "duration");
            if (duration != NULL)
                item->value = cbs_duplicate(duration->text);
        } else if (is_word(parser, "expect")) {
            advance(parser);
            if (is_word(parser, "exit")) {
                CbsToken *status;
                item = cbs_node_create(CBS_NODE_RUN_EXPECT, token->location);
                advance(parser);
                status = consume_kind(parser, CBS_TOKEN_INTEGER, "exit status");
                if (status != NULL)
                    item->number = strtol(status->text, NULL, 10);
            } else {
                CbsToken *pattern;
                item = cbs_node_create(CBS_NODE_RUN_STDOUT_ASSERT,
                                       token->location);
                consume_kind(parser, CBS_TOKEN_LBRACE, "{");
                consume_word(parser, "stdout");
                consume_word(parser, "contains");
                pattern = consume_text_value(parser, "stdout text");
                if (pattern != NULL) {
                    item->value = cbs_duplicate(pattern->text);
                    item->flag = pattern->kind;
                }
                consume_kind(parser, CBS_TOKEN_RBRACE, "}");
            }
        } else if (is_word(parser, "stdout")) {
            CbsToken *name;
            advance(parser);
            item = cbs_node_create(CBS_NODE_RUN_STDOUT_BIND, token->location);
            name = consume_kind(parser, CBS_TOKEN_STRING, "stdout binding name");
            if (name != NULL)
                item->name = cbs_duplicate(name->text);
        } else if (is_word(parser, "allow_failure")) {
            advance(parser);
            item = cbs_node_create(CBS_NODE_ALLOW_FAILURE, token->location);
            item->flag = diagnostic_only;
        } else {
            expected(parser, "run argument or option");
        }
        if (item != NULL)
            cbs_node_add(node, item);
    }
    consume_kind(parser, CBS_TOKEN_RBRACE, "}");
    return node;
}

/* Parse an environment-binding operation. */
static CbsNode *parse_env(CbsParser *parser) {
    CbsToken *keyword = consume_word(parser, "env");
    CbsToken *name;
    CbsToken *value;
    CbsNode *node = cbs_node_create(CBS_NODE_ENV, keyword->location);

    name = consume_kind(parser, CBS_TOKEN_STRING, "environment name string");
    consume_kind(parser, CBS_TOKEN_EQUAL, "=");
    value = consume_text_value(parser, "environment value");
    if (name != NULL)
        node->name = cbs_duplicate(name->text);
    if (value != NULL) {
        node->value = cbs_duplicate(value->text);
        node->flag = value->kind;
    }
    return node;
}

/* Consume a path value accepted by filesystem operations. */
static CbsToken *consume_path(CbsParser *parser) {
    CbsToken *token = current(parser);
    if (token->kind != CBS_TOKEN_STRING && token->kind != CBS_TOKEN_CBS_VALUE) {
        expected(parser, "path value");
        return NULL;
    }
    return advance(parser);
}

static CbsNode *parse_selector(CbsParser *parser, CbsNodeKind kind,
                               CbsLocation operation_location) {
    CbsNode *node = cbs_node_create(kind, operation_location);
    CbsToken *value;

    if (is_word(parser, "glob")) {
        advance(parser);
        node->flag = 1;
        value = consume_kind(parser, CBS_TOKEN_STRING, "glob string");
    } else {
        value = consume_path(parser);
    }
    if (value != NULL)
        node->value = cbs_duplicate(value->text);
    return node;
}

/* Parse an optional chmod mode attached to an operation. */
static void parse_optional_mode(CbsParser *parser, CbsNode *node) {
    int standalone_chmod = 0;

    if (is_word(parser, "chmod") && parser->cursor + 2 < parser->tokens.count) {
        const CbsToken *selector = &parser->tokens.items[parser->cursor + 2];
        standalone_chmod = selector->kind == CBS_TOKEN_STRING ||
                           selector->kind == CBS_TOKEN_CBS_VALUE ||
                           (selector->kind == CBS_TOKEN_WORD &&
                            strcmp(selector->text, "glob") == 0);
    }
    if (is_word(parser, "chmod") && !standalone_chmod) {
        CbsToken *mode;
        advance(parser);
        mode = consume_kind(parser, CBS_TOKEN_MODE, "permission mode");
        if (mode != NULL) {
            if (node->second_value == NULL) {
                node->second_value = cbs_duplicate(mode->text);
            } else {
                CbsNode *property = node_from_token(CBS_NODE_PROPERTY, mode);
                property->name = cbs_duplicate("chmod");
                cbs_node_add(node, property);
            }
        }
    }
}

/* Parse one filesystem operation and its path arguments. */
static CbsNode *parse_filesystem(CbsParser *parser) {
    CbsToken *keyword = current(parser);
    CbsToken *value;
    CbsNode *node;

    advance(parser);
    if (strcmp(keyword->text, "mkdir") == 0) {
        node = cbs_node_create(CBS_NODE_MKDIR, keyword->location);
        value = consume_path(parser);
        if (value != NULL)
            node->value = cbs_duplicate(value->text);
        parse_optional_mode(parser, node);
        return node;
    }
    if (strcmp(keyword->text, "copy") == 0 ||
        strcmp(keyword->text, "move") == 0) {
        node = parse_selector(
            parser,
            strcmp(keyword->text, "copy") == 0 ? CBS_NODE_COPY : CBS_NODE_MOVE,
            keyword->location);
        consume_word(parser, "to");
        value = consume_path(parser);
        if (value != NULL)
            node->second_value = cbs_duplicate(value->text);
        return node;
    }
    if (strcmp(keyword->text, "remove") == 0) {
        int tree = 0;
        if (is_word(parser, "tree")) {
            tree = 1;
            advance(parser);
        }
        node = parse_selector(parser, CBS_NODE_REMOVE, keyword->location);
        node->number = tree;
        return node;
    }
    if (strcmp(keyword->text, "symlink") == 0) {
        node = cbs_node_create(CBS_NODE_SYMLINK, keyword->location);
        value = consume_path(parser);
        if (value != NULL)
            node->value = cbs_duplicate(value->text);
        consume_word(parser, "to");
        value = consume_path(parser);
        if (value != NULL)
            node->second_value = cbs_duplicate(value->text);
        return node;
    }
    if (strcmp(keyword->text, "write") == 0) {
        node = cbs_node_create(CBS_NODE_WRITE, keyword->location);
        value = consume_path(parser);
        if (value != NULL)
            node->value = cbs_duplicate(value->text);
        value = consume_text_value(parser, "write value");
        if (value != NULL) {
            node->second_value = cbs_duplicate(value->text);
            node->flag = value->kind;
        }
        parse_optional_mode(parser, node);
        return node;
    }
    node = cbs_node_create(CBS_NODE_CHMOD, keyword->location);
    value = consume_kind(parser, CBS_TOKEN_MODE, "permission mode");
    if (value != NULL)
        node->second_value = cbs_duplicate(value->text);
    if (is_word(parser, "glob")) {
        advance(parser);
        node->flag = 1;
        value = consume_kind(parser, CBS_TOKEN_STRING, "glob string");
    } else {
        value = consume_path(parser);
    }
    if (value != NULL)
        node->value = cbs_duplicate(value->text);
    return node;
}

/* Parse an archive extraction operation. */
static CbsNode *parse_extract(CbsParser *parser) {
    CbsToken *keyword = consume_word(parser, "extract");
    CbsToken *source;
    CbsToken *destination;
    CbsNode *node = cbs_node_create(CBS_NODE_EXTRACT, keyword->location);

    source = consume_kind(parser, CBS_TOKEN_CBS_VALUE, "named source value");
    consume_word(parser, "into");
    destination = consume_path(parser);
    if (source != NULL)
        node->value = cbs_duplicate(source->text);
    if (destination != NULL)
        node->second_value = cbs_duplicate(destination->text);
    if (is_word(parser, "as")) {
        CbsToken *name;
        advance(parser);
        name =
            consume_kind(parser, CBS_TOKEN_STRING, "extracted directory name");
        if (name != NULL) {
            CbsNode *property = node_from_token(CBS_NODE_PROPERTY, name);
            property->name = cbs_duplicate("as");
            cbs_node_add(node, property);
        }
    }
    return node;
}

/* Parse a named-source materialization operation. */
static CbsNode *parse_materialize(CbsParser *parser) {
    CbsToken *keyword = consume_word(parser, "materialize");
    CbsToken *source =
        consume_kind(parser, CBS_TOKEN_CBS_VALUE, "named source value");
    CbsNode *node = cbs_node_create(CBS_NODE_MATERIALIZE, keyword->location);
    consume_word(parser, "to");
    {
        CbsToken *destination = consume_path(parser);
        if (source != NULL)
            node->value = cbs_duplicate(source->text);
        if (destination != NULL)
            node->second_value = cbs_duplicate(destination->text);
    }
    return node;
}

/* Parse replace/insert source-edit operations. */
static CbsNode *parse_edit(CbsParser *parser, int insert) {
    CbsToken *keyword = advance(parser);
    CbsToken *path;
    CbsToken *first;
    CbsToken *second;
    CbsToken *count;
    CbsNode *node = cbs_node_create(insert ? CBS_NODE_INSERT : CBS_NODE_REPLACE,
                                    keyword->location);

    path = consume_path(parser);
    consume_kind(parser, CBS_TOKEN_LBRACE, "{");
    consume_word(parser, insert ? "after" : "from");
    first = consume_text_value(parser, "match value");
    consume_word(parser, insert ? "write" : "to");
    second = consume_text_value(parser, "replacement value");
    consume_word(parser, "exactly");
    count = consume_kind(parser, CBS_TOKEN_INTEGER, "cardinality");
    consume_kind(parser, CBS_TOKEN_RBRACE, "}");
    if (path != NULL)
        node->name = cbs_duplicate(path->text);
    if (first != NULL)
        node->value = cbs_duplicate(first->text);
    if (first != NULL)
        node->flag = first->kind;
    if (second != NULL) {
        node->second_value = cbs_duplicate(second->text);
        node->second_flag = second->kind;
    }
    if (count != NULL)
        node->number = strtol(count->text, NULL, 10);
    return node;
}

/* Parse a runtime assertion and its property block. */
static CbsNode *parse_require(CbsParser *parser) {
    CbsToken *keyword = consume_word(parser, "require");
    CbsToken *kind;
    CbsToken *target;
    CbsNode *node = cbs_node_create(CBS_NODE_REQUIRE, keyword->location);

    kind = consume_kind(parser, CBS_TOKEN_WORD, "file, directory, or glob");
    if (kind != NULL)
        node->name = cbs_duplicate(kind->text);
    if (kind != NULL && strcmp(kind->text, "glob") == 0)
        target = consume_kind(parser, CBS_TOKEN_STRING, "glob string");
    else
        target = consume_path(parser);
    if (target != NULL)
        node->value = cbs_duplicate(target->text);
    consume_kind(parser, CBS_TOKEN_LBRACE, "{");
    if (kind != NULL && strcmp(kind->text, "config") == 0) {
        while (!parser->failed && current(parser)->kind != CBS_TOKEN_RBRACE &&
               current(parser)->kind != CBS_TOKEN_EOF) {
            CbsToken *symbol =
                consume_kind(parser, CBS_TOKEN_WORD, "configuration symbol");
            CbsNode *property;
            CbsToken *state;
            consume_kind(parser, CBS_TOKEN_EQUAL, "=");
            state = consume_kind(parser, CBS_TOKEN_WORD, "configuration state");
            if (symbol == NULL || state == NULL)
                continue;
            property = cbs_node_create(CBS_NODE_PROPERTY, symbol->location);
            property->name = cbs_duplicate(symbol->text);
            property->value = cbs_duplicate(state->text);
            property->flag = state->kind;
            cbs_node_add(node, property);
        }
    } else if (kind != NULL && strcmp(kind->text, "glob") == 0) {
        CbsToken *count;
        consume_word(parser, "count");
        count = consume_kind(parser, CBS_TOKEN_INTEGER, "cardinality");
        if (count != NULL)
            node->number = strtol(count->text, NULL, 10);
    } else {
        consume_word(parser, "exists");
        while (is_word(parser, "contains") || is_word(parser, "same_as") ||
               is_word(parser, "nonempty")) {
            CbsToken *text;
            CbsNode *property;
            int same_as = is_word(parser, "same_as");
            int nonempty = is_word(parser, "nonempty");
            advance(parser);
            text = nonempty ? NULL
                            : (same_as ? consume_path(parser)
                                       : consume_text_value(parser, "contained value"));
            property = text == NULL ? cbs_node_create(CBS_NODE_PROPERTY,
                                                       keyword->location)
                                    : node_from_token(CBS_NODE_PROPERTY, text);
            property->name = cbs_duplicate(nonempty ? "nonempty"
                                                     : (same_as ? "same_as" : "contains"));
            cbs_node_add(node, property);
        }
    }
    consume_kind(parser, CBS_TOKEN_RBRACE, "}");
    return node;
}

/* Parse any operation allowed in the current block. */
static CbsNode *parse_operation(CbsParser *parser, int diagnostic_only) {
    CbsToken *keyword = current(parser);
    CbsNode *node;
    CbsToken *path;

    if (is_word(parser, "run"))
        return parse_run(parser, diagnostic_only);
    if (is_word(parser, "require"))
        return parse_require(parser);
    if (diagnostic_only) {
        expected(parser, "run, require, or }");
        return NULL;
    }
    if (is_word(parser, "env"))
        return parse_env(parser);
    if (is_word(parser, "cd")) {
        advance(parser);
        node = cbs_node_create(CBS_NODE_CD, keyword->location);
        path = consume_path(parser);
        if (path != NULL)
            node->value = cbs_duplicate(path->text);
        return parse_operation_block(parser, node, 0);
    }
    if (is_word(parser, "mkdir") || is_word(parser, "copy") ||
        is_word(parser, "move") || is_word(parser, "remove") ||
        is_word(parser, "symlink") || is_word(parser, "write") ||
        is_word(parser, "chmod"))
        return parse_filesystem(parser);
    if (is_word(parser, "extract"))
        return parse_extract(parser);
    if (is_word(parser, "materialize"))
        return parse_materialize(parser);
    if (is_word(parser, "replace"))
        return parse_edit(parser, 0);
    if (is_word(parser, "insert"))
        return parse_edit(parser, 1);
    expected(parser, "phase operation");
    return NULL;
}

static CbsNode *parse_operation_block(CbsParser *parser, CbsNode *owner,
                                      int diagnostic_only) {
    if (consume_kind(parser, CBS_TOKEN_LBRACE, "{") == NULL)
        return owner;
    while (!parser->failed && current(parser)->kind != CBS_TOKEN_RBRACE &&
           current(parser)->kind != CBS_TOKEN_EOF) {
        CbsNode *child;

        if (!diagnostic_only && is_word(parser, "on_fail")) {
            CbsToken *keyword = advance(parser);
            CbsNode *on_fail =
                cbs_node_create(CBS_NODE_ON_FAIL, keyword->location);
            parse_operation_block(parser, on_fail, 1);
            cbs_node_add(owner, on_fail);
            if (current(parser)->kind != CBS_TOKEN_RBRACE)
                expected(parser, "}");
            break;
        }
        child = parse_operation(parser, diagnostic_only);
        if (child != NULL)
            cbs_node_add(owner, child);
    }
    consume_kind(parser, CBS_TOKEN_RBRACE, "}");
    return owner;
}

/* Parse the package's named source declarations. */
static CbsNode *parse_sources(CbsParser *parser) {
    CbsToken *keyword = consume_word(parser, "sources");
    CbsNode *node = cbs_node_create(CBS_NODE_SOURCES, keyword->location);

    consume_kind(parser, CBS_TOKEN_LBRACE, "{");
    while (!parser->failed && current(parser)->kind != CBS_TOKEN_RBRACE &&
           current(parser)->kind != CBS_TOKEN_EOF) {
        CbsToken *kind = current(parser);
        CbsToken *name;
        CbsToken *hash;
        CbsNode *source;

        if (!is_word(parser, "main") && !is_word(parser, "extra")) {
            expected(parser, "main, extra, or }");
            break;
        }
        advance(parser);
        source = cbs_node_create(CBS_NODE_SOURCE, kind->location);
        source->name = cbs_duplicate(kind->text);
        name = consume_kind(parser, CBS_TOKEN_STRING, "source name");
        if (name != NULL)
            source->value = cbs_duplicate(name->text);
        consume_kind(parser, CBS_TOKEN_LBRACE, "{");
        if (!is_word(parser, "url"))
            expected(parser, "url");
        while (!parser->failed && is_word(parser, "url")) {
            CbsToken *url;
            advance(parser);
            url = consume_kind(parser, CBS_TOKEN_STRING, "source URL");
            if (url != NULL)
                cbs_node_add(source, node_from_token(CBS_NODE_URL, url));
        }
        consume_word(parser, "sha256");
        hash = consume_kind(parser, CBS_TOKEN_STRING, "SHA-256 string");
        consume_kind(parser, CBS_TOKEN_RBRACE, "}");
        if (hash != NULL)
            cbs_node_add(source, node_from_token(CBS_NODE_SHA256, hash));
        cbs_node_add(node, source);
    }
    consume_kind(parser, CBS_TOKEN_RBRACE, "}");
    return node;
}

/* Parse dependency groups and their typed requirements. */
static CbsNode *parse_requires(CbsParser *parser) {
    CbsToken *keyword = consume_word(parser, "requires");
    CbsNode *node = cbs_node_create(CBS_NODE_REQUIRES, keyword->location);

    consume_kind(parser, CBS_TOKEN_LBRACE, "{");
    while (!parser->failed && current(parser)->kind != CBS_TOKEN_RBRACE &&
           current(parser)->kind != CBS_TOKEN_EOF) {
        CbsToken *role = current(parser);
        CbsNode *group;

        if (!is_word(parser, "build") && !is_word(parser, "runtime") &&
            !is_word(parser, "test") && !is_word(parser, "bootstrap")) {
            expected(parser, "dependency role or }");
            break;
        }
        advance(parser);
        group = cbs_node_create(CBS_NODE_DEPENDENCY_GROUP, role->location);
        group->name = cbs_duplicate(role->text);
        consume_kind(parser, CBS_TOKEN_LBRACE, "{");
        while (!parser->failed && current(parser)->kind != CBS_TOKEN_RBRACE &&
               current(parser)->kind != CBS_TOKEN_EOF) {
            CbsToken *kind = current(parser);
            CbsToken *name;
            CbsNode *dependency;

            if (!is_word(parser, "tool") && !is_word(parser, "library") &&
                !is_word(parser, "headers") && !is_word(parser, "compiler") &&
                !is_word(parser, "package")) {
                expected(parser, "dependency kind or }");
                break;
            }
            advance(parser);
            name = consume_kind(parser, CBS_TOKEN_STRING, "dependency name");
            dependency = cbs_node_create(CBS_NODE_DEPENDENCY, kind->location);
            dependency->name = cbs_duplicate(kind->text);
            if (name != NULL)
                dependency->value = cbs_duplicate(name->text);
            cbs_node_add(group, dependency);
        }
        consume_kind(parser, CBS_TOKEN_RBRACE, "}");
        cbs_node_add(node, group);
    }
    consume_kind(parser, CBS_TOKEN_RBRACE, "}");
    return node;
}

/* Identify one of CPDL's five phase keywords. */
static int phase_word(CbsParser *parser) {
    return is_word(parser, "prepare") || is_word(parser, "configure") ||
           is_word(parser, "build") || is_word(parser, "check") ||
           is_word(parser, "install");
}

/* Parse build-image, capability, toolchain, or upstream metadata. */
static CbsNode *parse_build_metadata(CbsParser *parser, CbsNodeKind kind) {
    CbsToken *keyword = current(parser);
    CbsToken *value;
    CbsNode *node;

    advance(parser);
    value = consume_kind(parser, CBS_TOKEN_STRING, "metadata value");
    node = cbs_node_create(kind, keyword->location);
    if (value != NULL)
        node->value = cbs_duplicate(value->text);
    if (kind == CBS_NODE_TOOLCHAIN) {
        consume_kind(parser, CBS_TOKEN_LBRACE, "{");
        consume_word(parser, "reason");
        value = consume_kind(parser, CBS_TOKEN_STRING,
                             "toolchain exception reason");
        if (value != NULL) {
            CbsNode *reason = node_from_token(CBS_NODE_PROPERTY, value);
            reason->name = cbs_duplicate("reason");
            cbs_node_add(node, reason);
        }
        consume_kind(parser, CBS_TOKEN_RBRACE, "}");
    }
    return node;
}

/* Parse an opaque package metadata block of string key/value pairs. */
static CbsNode *parse_opaque_metadata(CbsParser *parser) {
    CbsToken *keyword = current(parser);
    CbsNode *node = cbs_node_create(CBS_NODE_METADATA, keyword->location);
    advance(parser);
    consume_kind(parser, CBS_TOKEN_LBRACE, "{");
    while (!parser->failed && current(parser)->kind != CBS_TOKEN_RBRACE &&
           current(parser)->kind != CBS_TOKEN_EOF) {
        CbsToken *key = consume_kind(parser, CBS_TOKEN_STRING,
                                     "metadata key");
        CbsToken *value = consume_kind(parser, CBS_TOKEN_STRING,
                                       "metadata value");
        CbsNode *property;
        if (key == NULL || value == NULL)
            break;
        property = cbs_node_create(CBS_NODE_PROPERTY, key->location);
        property->name = cbs_duplicate(key->text);
        property->value = cbs_duplicate(value->text);
        cbs_node_add(node, property);
    }
    consume_kind(parser, CBS_TOKEN_RBRACE, "}");
    return node;
}

/* Parse the next package-level declaration or phase. */
static CbsNode *parse_package_item(CbsParser *parser) {
    CbsToken *keyword = current(parser);
    CbsToken *value;
    CbsNode *node;

    if (is_word(parser, "version")) {
        advance(parser);
        value = consume_kind(parser, CBS_TOKEN_STRING, "version string");
        return value == NULL ? NULL : node_from_token(CBS_NODE_VERSION, value);
    }
    if (is_word(parser, "release")) {
        advance(parser);
        value = consume_kind(parser, CBS_TOKEN_INTEGER, "release integer");
        node = cbs_node_create(CBS_NODE_RELEASE, keyword->location);
        if (value != NULL) {
            node->value = cbs_duplicate(value->text);
            node->number = strtol(value->text, NULL, 10);
        }
        return node;
    }
    if (is_word(parser, "format")) {
        advance(parser);
        value = consume_kind(parser, CBS_TOKEN_STRING, "artifact format");
        return value == NULL ? NULL : node_from_token(CBS_NODE_FORMAT, value);
    }
    if (is_word(parser, "architecture")) {
        advance(parser);
        node = cbs_node_create(CBS_NODE_ARCHITECTURE, keyword->location);
        if (is_word(parser, "any"))
            value = advance(parser);
        else
            value =
                consume_kind(parser, CBS_TOKEN_STRING, "architecture or any");
        if (value != NULL)
            node->value = cbs_duplicate(value->text);
        return node;
    }
    if (is_word(parser, "sources"))
        return parse_sources(parser);
    if (is_word(parser, "requires"))
        return parse_requires(parser);
    if (is_word(parser, "build_image"))
        return parse_build_metadata(parser, CBS_NODE_BUILD_IMAGE);
    if (is_word(parser, "capability"))
        return parse_build_metadata(parser, CBS_NODE_CAPABILITY);
    if (is_word(parser, "toolchain"))
        return parse_build_metadata(parser, CBS_NODE_TOOLCHAIN);
    if (is_word(parser, "upstream"))
        return parse_build_metadata(parser, CBS_NODE_UPSTREAM);
    if (is_word(parser, "metadata"))
        return parse_opaque_metadata(parser);
    if (phase_word(parser)) {
        advance(parser);
        node = cbs_node_create(CBS_NODE_PHASE, keyword->location);
        node->name = cbs_duplicate(keyword->text);
        return parse_operation_block(parser, node, 0);
    }
    expected(parser, "package declaration");
    return NULL;
}

/* Parse the complete CPDL document and its single package. */
static CbsNode *parse_document(CbsParser *parser) {
    CbsToken *package_keyword;
    CbsToken *name;
    CbsNode *document;
    CbsNode *package;

    package_keyword = consume_word(parser, "package");
    if (package_keyword == NULL)
        return NULL;
    name = consume_kind(parser, CBS_TOKEN_STRING, "package name");
    if (name == NULL)
        return NULL;
    document = cbs_node_create(CBS_NODE_DOCUMENT, package_keyword->location);
    package = cbs_node_create(CBS_NODE_PACKAGE, package_keyword->location);
    package->value = cbs_duplicate(name->text);
    cbs_node_add(document, package);
    consume_kind(parser, CBS_TOKEN_LBRACE, "{");
    while (!parser->failed && current(parser)->kind != CBS_TOKEN_RBRACE &&
           current(parser)->kind != CBS_TOKEN_EOF) {
        CbsNode *item = parse_package_item(parser);
        if (item != NULL)
            cbs_node_add(package, item);
    }
    consume_kind(parser, CBS_TOKEN_RBRACE, "}");
    consume_kind(parser, CBS_TOKEN_EOF, "end of file");
    return document;
}

CbsNode *cbs_parse(const char *path, const char *source, size_t length,
                   CbsTokenList *tokens) {
    CbsParser parser;
    CbsNode *document;

    (void)length;
    memset(&parser, 0, sizeof(parser));
    parser.path = path;
    parser.source = source;
    parser.source_length = length;
    parser.tokens = *tokens;
    document = parse_document(&parser);
    if (parser.failed) {
        cbs_node_destroy(document);
        return NULL;
    }
    return document;
}
