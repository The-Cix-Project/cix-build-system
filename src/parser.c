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

static CbsNode *clone_node(const CbsNode *source) {
    CbsNode *copy = cbs_node_create(source->kind, source->location);
    size_t index;
    copy->name = source->name == NULL ? NULL : cbs_duplicate(source->name);
    copy->value = source->value == NULL ? NULL : cbs_duplicate(source->value);
    copy->second_value = source->second_value == NULL
                             ? NULL
                             : cbs_duplicate(source->second_value);
    copy->number = source->number;
    copy->flag = source->flag;
    copy->second_flag = source->second_flag;
    copy->selector_glob = source->selector_glob;
    copy->insert_before = source->insert_before;
    copy->stderr_stream = source->stderr_stream;
    for (index = 0; index < source->child_count; ++index)
        cbs_node_add(copy, clone_node(source->children[index]));
    return copy;
}

/* Parse a process operation and its arguments/options. */
static CbsNode *parse_run(CbsParser *parser, int diagnostic_only) {
    CbsToken *keyword = consume_word(parser, "run");
    CbsToken *program;
    CbsNode *node;
    CbsNode *each_values = NULL;

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
        } else if (is_word(parser, "args")) {
            CbsToken *pattern;
            advance(parser);
            item = cbs_node_create(CBS_NODE_RUN_GLOB, token->location);
            consume_word(parser, "glob");
            pattern = consume_kind(parser, CBS_TOKEN_STRING, "glob string");
            item->number = -1;
            if (pattern != NULL)
                item->value = cbs_duplicate(pattern->text);
            if (is_word(parser, "exactly")) {
                CbsToken *count;
                advance(parser);
                count = consume_kind(parser, CBS_TOKEN_INTEGER,
                                      "glob cardinality");
                if (count != NULL)
                    item->number = strtol(count->text, NULL, 10);
            }
        } else if (is_word(parser, "each")) {
            advance(parser);
            each_values = cbs_node_create(CBS_NODE_LIST, token->location);
            while (is_text_value(current(parser)))
                cbs_node_add(each_values,
                             node_from_token(CBS_NODE_ARGUMENT, advance(parser)));
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
                if (is_word(parser, "stderr")) {
                    advance(parser);
                    item->stderr_stream = 1;
                } else {
                    consume_word(parser, "stdout");
                }
                consume_word(parser, "contains");
                pattern = consume_text_value(parser, "stdout text");
                if (pattern != NULL) {
                    item->value = cbs_duplicate(pattern->text);
                    item->flag = pattern->kind;
                }
                consume_kind(parser, CBS_TOKEN_RBRACE, "}");
            }
        } else if (is_word(parser, "stdout") || is_word(parser, "stderr")) {
            CbsToken *name;
            int stderr_stream = is_word(parser, "stderr");
            advance(parser);
            if (is_word(parser, "file")) {
                CbsToken *path;
                advance(parser);
                item = cbs_node_create(CBS_NODE_RUN_STDOUT_FILE,
                                       token->location);
                item->stderr_stream = stderr_stream;
                path = consume_text_value(parser, "stdout file path");
                if (path != NULL) {
                    item->value = cbs_duplicate(path->text);
                    item->flag = path->kind;
                }
            } else {
                item = cbs_node_create(CBS_NODE_RUN_STDOUT_BIND,
                                       token->location);
                item->stderr_stream = stderr_stream;
                name = consume_kind(parser, CBS_TOKEN_STRING,
                                    "stdout binding name");
                if (name != NULL)
                    item->name = cbs_duplicate(name->text);
            }
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
    if (each_values != NULL) {
        CbsNode *list = cbs_node_create(CBS_NODE_LIST, keyword->location);
        size_t index;
        for (index = 0; index < each_values->child_count; ++index) {
            CbsNode *expanded = clone_node(node);
            cbs_node_add(expanded, clone_node(each_values->children[index]));
            cbs_node_add(list, expanded);
        }
        cbs_node_destroy(each_values);
        cbs_node_destroy(node);
        return list;
    }
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
        int tree = 0;
        if (strcmp(keyword->text, "copy") == 0 && is_word(parser, "tree")) {
            tree = 1;
            advance(parser);
        }
        node = parse_selector(
            parser,
            strcmp(keyword->text, "copy") == 0 ? CBS_NODE_COPY : CBS_NODE_MOVE,
            keyword->location);
        node->number = tree;
        consume_word(parser, "to");
        value = consume_path(parser);
        if (value != NULL)
            node->second_value = cbs_duplicate(value->text);
        if (is_word(parser, "allow_failure")) {
            node->second_flag = 1;
            advance(parser);
        }
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
        if (is_word(parser, "allow_failure")) {
            node->second_flag = 1;
            advance(parser);
        }
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
        if (value != NULL) {
            node->value = cbs_duplicate(value->text);
            node->flag = value->kind;
        }
        value = consume_text_value(parser, "write value");
        if (value != NULL) {
            node->second_value = cbs_duplicate(value->text);
            node->second_flag = value->kind;
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

    if (is_word(parser, "glob")) {
        advance(parser);
        path = consume_kind(parser, CBS_TOKEN_STRING, "glob string");
        node->selector_glob = 1;
    } else {
        path = consume_path(parser);
    }
    consume_kind(parser, CBS_TOKEN_LBRACE, "{");
    if (insert && is_word(parser, "before")) {
        advance(parser);
        node->insert_before = 1;
    } else {
        consume_word(parser, insert ? "after" : "from");
    }
    first = consume_text_value(parser, "match value");
    if (is_word(parser, "until")) {
        /* The match extends from the literal prefix to a delimiter. */
        CbsToken *until = advance(parser);
        CbsToken *delimiter;
        if (insert) {
            expected(parser, "write (until applies only to replace)");
            return node;
        }
        if (!is_word(parser, "whitespace") && !is_word(parser, "line")) {
            expected(parser, "whitespace or line");
            return node;
        }
        delimiter = advance(parser);
        {
            CbsNode *property =
                cbs_node_create(CBS_NODE_PROPERTY, until->location);
            property->name = cbs_duplicate("until");
            property->value = cbs_duplicate(delimiter->text);
            cbs_node_add(node, property);
        }
    }
    if (is_word(parser, "at")) {
        CbsToken *at = advance(parser);
        CbsNode *property = cbs_node_create(CBS_NODE_PROPERTY, at->location);
        property->name = cbs_duplicate("at");
        if (is_word(parser, "line_start")) {
            property->value = cbs_duplicate(advance(parser)->text);
        } else {
            CbsToken *line = consume_word(parser, "line");
            CbsToken *number = consume_kind(parser, CBS_TOKEN_INTEGER,
                                             "line number");
            property->value = cbs_duplicate("line");
            if (line != NULL && number != NULL)
                property->number = strtol(number->text, NULL, 10);
        }
        cbs_node_add(node, property);
    }
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

/* Parse `truncate PATH { from MATCH exactly COUNT }`. */
static CbsNode *parse_truncate(CbsParser *parser) {
    CbsToken *keyword = advance(parser);
    CbsToken *path;
    CbsToken *match;
    CbsToken *count;
    CbsNode *node = cbs_node_create(CBS_NODE_TRUNCATE, keyword->location);

    path = consume_path(parser);
    consume_kind(parser, CBS_TOKEN_LBRACE, "{");
    consume_word(parser, "from");
    match = consume_text_value(parser, "match value");
    consume_word(parser, "exactly");
    count = consume_kind(parser, CBS_TOKEN_INTEGER, "cardinality");
    consume_kind(parser, CBS_TOKEN_RBRACE, "}");
    if (path != NULL)
        node->name = cbs_duplicate(path->text);
    if (match != NULL) {
        node->value = cbs_duplicate(match->text);
        node->flag = match->kind;
    }
    if (count != NULL)
        node->number = strtol(count->text, NULL, 10);
    return node;
}

/* Parse `glob "NAME" = PATTERN [exactly COUNT]`. */
static CbsNode *parse_glob_binding(CbsParser *parser) {
    CbsToken *keyword = advance(parser);
    CbsToken *name = consume_kind(parser, CBS_TOKEN_STRING, "glob binding name");
    CbsToken *pattern;
    CbsToken *count = NULL;
    CbsNode *node = cbs_node_create(CBS_NODE_GLOB_BIND, keyword->location);

    consume_kind(parser, CBS_TOKEN_EQUAL, "=");
    pattern = consume_text_value(parser, "glob pattern");
    if (is_word(parser, "exactly")) {
        advance(parser);
        count = consume_kind(parser, CBS_TOKEN_INTEGER, "cardinality");
    }
    if (name != NULL)
        node->name = cbs_duplicate(name->text);
    if (pattern != NULL) {
        node->value = cbs_duplicate(pattern->text);
        node->flag = pattern->kind;
    }
    node->number = count == NULL ? 1 : strtol(count->text, NULL, 10);
    return node;
}

static CbsNode *parse_links(CbsParser *parser) {
    CbsToken *keyword = advance(parser);
    CbsToken *path = consume_path(parser);
    CbsNode *node = cbs_node_create(CBS_NODE_LINKS, keyword->location);
    if (path != NULL)
        node->value = cbs_duplicate(path->text);
    if (consume_kind(parser, CBS_TOKEN_LBRACE, "{") != NULL)
        while (!parser->failed && current(parser)->kind != CBS_TOKEN_RBRACE &&
               current(parser)->kind != CBS_TOKEN_EOF) {
            CbsToken *property = advance(parser);
            CbsNode *item;
            if (strcmp(property->text, "needs") == 0 ||
                       strcmp(property->text, "forbids") == 0) {
                CbsToken *value = consume_text_value(parser, "library name");
                item = cbs_node_create(CBS_NODE_PROPERTY, property->location);
                item->name = cbs_duplicate(property->text);
                if (value != NULL)
                    item->value = cbs_duplicate(value->text);
            } else {
                expected(parser, "needs or forbids");
                continue;
            }
            cbs_node_add(node, item);
        }
    consume_kind(parser, CBS_TOKEN_RBRACE, "}");
    return node;
}

static CbsNode *parse_patch(CbsParser *parser) {
    CbsToken *keyword = advance(parser);
    CbsToken *file = consume_kind(parser, CBS_TOKEN_STRING, "patch file");
    CbsToken *digest = NULL;
    CbsToken *strip = NULL;
    CbsNode *node = cbs_node_create(CBS_NODE_PATCH, keyword->location);
    consume_kind(parser, CBS_TOKEN_LBRACE, "{");
    consume_word(parser, "sha256");
    digest = consume_kind(parser, CBS_TOKEN_STRING, "patch digest");
    if (is_word(parser, "strip")) {
        advance(parser);
        strip = consume_kind(parser, CBS_TOKEN_INTEGER, "strip count");
    }
    consume_kind(parser, CBS_TOKEN_RBRACE, "}");
    if (file != NULL)
        node->name = cbs_duplicate(file->text);
    if (digest != NULL)
        node->value = cbs_duplicate(digest->text);
    node->number = strip == NULL ? 0 : strtol(strip->text, NULL, 10);
    return node;
}

/* Parse a runtime assertion and its property block. */
static CbsNode *parse_require(CbsParser *parser) {
    CbsToken *keyword = consume_word(parser, "require");
    CbsToken *kind;
    CbsToken *target;
    CbsNode *node = cbs_node_create(CBS_NODE_REQUIRE, keyword->location);

    kind = consume_kind(parser, CBS_TOKEN_WORD,
                        "file, directory, symlink, glob, or config");
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
        if (is_word(parser, "exactly"))
            advance(parser);
        else
            consume_word(parser, "count");
        count = consume_kind(parser, CBS_TOKEN_INTEGER, "cardinality");
        if (count != NULL)
            node->number = strtol(count->text, NULL, 10);
    } else {
        consume_word(parser, "exists");
        while (is_word(parser, "contains") || is_word(parser, "same_as") ||
               is_word(parser, "nonempty") || is_word(parser, "executable") ||
               is_word(parser, "target")) {
            CbsToken *text;
            CbsNode *property;
            const char *property_name = current(parser)->text;
            int path_valued = strcmp(property_name, "same_as") == 0 ||
                              strcmp(property_name, "target") == 0;
            int nonempty = strcmp(property_name, "nonempty") == 0 ||
                           strcmp(property_name, "executable") == 0;
            advance(parser);
            text = nonempty ? NULL
                   : path_valued
                       ? consume_path(parser)
                       : consume_text_value(parser, "contained value");
            property = text == NULL ? cbs_node_create(CBS_NODE_PROPERTY,
                                                       keyword->location)
                                    : node_from_token(CBS_NODE_PROPERTY, text);
            property->name = cbs_duplicate(property_name);
            cbs_node_add(node, property);
        }
    }
    consume_kind(parser, CBS_TOKEN_RBRACE, "}");
    if (is_word(parser, "for")) {
        CbsNode *values;
        CbsNode *list;
        size_t index;
        advance(parser);
        consume_kind(parser, CBS_TOKEN_LBRACE, "{");
        values = cbs_node_create(CBS_NODE_LIST, keyword->location);
        while (is_text_value(current(parser)))
            cbs_node_add(values,
                         node_from_token(CBS_NODE_ARGUMENT, advance(parser)));
        consume_kind(parser, CBS_TOKEN_RBRACE, "}");
        list = cbs_node_create(CBS_NODE_LIST, keyword->location);
        for (index = 0; index < values->child_count; ++index) {
            CbsNode *expanded = clone_node(node);
            free(expanded->value);
            expanded->value = cbs_duplicate(values->children[index]->value);
            cbs_node_add(list, expanded);
        }
        cbs_node_destroy(values);
        cbs_node_destroy(node);
        return list;
    }
    return node;
}

/* Report a located each-declaration rule violation and mark parsing failed. */
static void parse_error(CbsParser *parser, CbsLocation location,
                        const char *message) {
    if (!parser->failed)
        cbs_diagnostic(parser->path, parser->source, location, "error",
                       "CPDL-E2003", CBS_DIAG_PARSE, message);
    parser->failed = 1;
}

/* An each item name is an identifier: letters, digits, and underscores. */
static int valid_item_name(const char *name) {
    size_t index;
    if (name == NULL || name[0] == '\0')
        return 0;
    for (index = 0; name[index] != '\0'; ++index) {
        unsigned char byte = (unsigned char)name[index];
        int alpha = (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z');
        int digit = byte >= '0' && byte <= '9';
        if (!alpha && byte != '_' && (index == 0 || !digit))
            return 0;
    }
    return 1;
}

/* Return TEXT with every PLACEHOLDER replaced by ITEM, or NULL when TEXT
 * does not contain it. */
static char *replace_placeholder(const char *text, const char *placeholder,
                                 const char *item) {
    size_t placeholder_length = strlen(placeholder);
    size_t item_length = strlen(item);
    size_t occurrences = 0;
    const char *cursor;
    char *result;
    char *output;

    for (cursor = strstr(text, placeholder); cursor != NULL;
         cursor = strstr(cursor + placeholder_length, placeholder))
        ++occurrences;
    if (occurrences == 0)
        return NULL;
    result = cbs_allocate(strlen(text) + occurrences * item_length + 1);
    output = result;
    cursor = text;
    while (*cursor != '\0') {
        const char *next = strstr(cursor, placeholder);
        size_t plain = next == NULL ? strlen(cursor) : (size_t)(next - cursor);
        memcpy(output, cursor, plain);
        output += plain;
        if (next == NULL)
            break;
        memcpy(output, item, item_length);
        output += item_length;
        cursor = next + placeholder_length;
    }
    *output = '\0';
    return result;
}

/* Return one path-derived view of an each item. These are lexical operations;
 * they do not inspect the build filesystem. */
static char *item_accessor(const char *item, const char *accessor) {
    const char *slash = strrchr(item, '/');
    const char *base = slash == NULL ? item : slash + 1;
    const char *dot;

    if (strcmp(accessor, "basename") == 0)
        return cbs_duplicate(base);
    if (strcmp(accessor, "dirname") == 0) {
        if (slash == NULL)
            return cbs_duplicate(".");
        if (slash == item)
            return cbs_duplicate("/");
        return cbs_duplicate_range(item, (size_t)(slash - item));
    }
    if (strcmp(accessor, "stem") == 0) {
        dot = strrchr(base, '.');
        if (dot == NULL || dot == base)
            return cbs_duplicate(base);
        return cbs_duplicate_range(base, (size_t)(dot - base));
    }
    return NULL;
}

/* Replace `${each.NAME}` and its lexical path accessors in TEXT. */
static char *replace_item_placeholders(const char *text, const char *name,
                                       const char *item, int *invalid) {
    char prefix[256];
    size_t prefix_length;
    size_t item_length = strlen(item);
    size_t result_size;
    size_t length = 0;
    const char *cursor = text;
    char *result;
    int changed = 0;

    if (snprintf(prefix, sizeof(prefix), "${each.%s", name) >=
        (int)sizeof(prefix))
        return NULL;
    prefix_length = strlen(prefix);
    /* There can be many placeholders. Each replacement is no longer than the
     * complete item, so this conservative bound keeps the operation local and
     * avoids reallocating while walking the string. */
    result_size = strlen(text) + (strlen(text) / 2 + 1) * item_length + 1;
    result = cbs_allocate(result_size);
    while (*cursor != '\0') {
        const char *opening = strstr(cursor, "${");
        const char *end;
        const char *suffix;
        char *replacement = NULL;
        size_t plain;

        if (opening == NULL) {
            plain = strlen(cursor);
            memcpy(result + length, cursor, plain);
            length += plain;
            break;
        }
        plain = (size_t)(opening - cursor);
        memcpy(result + length, cursor, plain);
        length += plain;
        end = strchr(opening + 2, '}');
        if (end == NULL) {
            plain = strlen(opening);
            memcpy(result + length, opening, plain);
            length += plain;
            break;
        }
        suffix = opening + prefix_length;
        if (strncmp(opening, prefix, prefix_length) == 0 &&
            (suffix == end || *suffix == '.')) {
            if (suffix == end)
                replacement = cbs_duplicate(item);
            else if (suffix + 1 < end) {
                char accessor[32];
                size_t accessor_length = (size_t)(end - suffix - 1);
                if (accessor_length < sizeof(accessor)) {
                    memcpy(accessor, suffix + 1, accessor_length);
                    accessor[accessor_length] = '\0';
                    replacement = item_accessor(item, accessor);
                }
                if (replacement == NULL)
                    *invalid = 1;
            } else {
                *invalid = 1;
            }
            if (replacement != NULL) {
                size_t replacement_length = strlen(replacement);
                memcpy(result + length, replacement, replacement_length);
                length += replacement_length;
                free(replacement);
                changed = 1;
            } else {
                plain = (size_t)(end + 1 - opening);
                memcpy(result + length, opening, plain);
                length += plain;
            }
        } else {
            plain = (size_t)(end + 1 - opening);
            memcpy(result + length, opening, plain);
            length += plain;
        }
        cursor = end + 1;
    }
    result[length] = '\0';
    if (!changed) {
        free(result);
        return NULL;
    }
    return result;
}

/* Bind one item throughout a cloned each body. Every quoted-string field
 * has `${each.NAME}` or a path accessor replaced; block strings do not
 * interpolate and are left alone. */
static void bind_item(CbsNode *node, const char *name, const char *item,
                      int *invalid) {
    size_t index;
    char *bound;

    if (node->name != NULL &&
        (bound = replace_item_placeholders(node->name, name, item, invalid)) !=
            NULL) {
        free(node->name);
        node->name = bound;
    }
    if (node->value != NULL && node->flag != CBS_TOKEN_BLOCK_STRING &&
        (bound = replace_item_placeholders(node->value, name, item, invalid)) !=
            NULL) {
        free(node->value);
        node->value = bound;
    }
    if (node->second_value != NULL &&
        node->second_flag != CBS_TOKEN_BLOCK_STRING &&
        (bound = replace_item_placeholders(node->second_value, name, item,
                                           invalid)) != NULL) {
        free(node->second_value);
        node->second_value = bound;
    }
    for (index = 0; index < node->child_count; ++index)
        bind_item(node->children[index], name, item, invalid);
}

/* True when an already expanded nested each inside NODE bound NAME. */
static int binds_name(const CbsNode *node, const char *name) {
    size_t index;
    if (node->kind == CBS_NODE_LIST && node->name != NULL &&
        strcmp(node->name, name) == 0)
        return 1;
    for (index = 0; index < node->child_count; ++index)
        if (binds_name(node->children[index], name))
            return 1;
    return 0;
}

/* Parse `each "NAME" in { "item" ... } { body }` and expand it at parse time
 * into one named block per item. The result is a list whose children are
 * those blocks; each block records the bound name, the item, and its
 * ordinal so a runtime failure can name them. */
static CbsNode *parse_each(CbsParser *parser, int diagnostic_only) {
    CbsToken *keyword = consume_word(parser, "each");
    CbsToken *name;
    CbsNode *items;
    CbsNode *body;
    CbsNode *expansion;
    char placeholder[256];
    size_t index;
    int invalid_accessor = 0;

    name = consume_kind(parser, CBS_TOKEN_STRING, "each item name string");
    consume_word(parser, "in");
    items = cbs_node_create(CBS_NODE_LIST, keyword->location);
    if (consume_kind(parser, CBS_TOKEN_LBRACE, "{") != NULL) {
        while (current(parser)->kind == CBS_TOKEN_STRING)
            cbs_node_add(items,
                         node_from_token(CBS_NODE_ARGUMENT, advance(parser)));
        if (current(parser)->kind != CBS_TOKEN_RBRACE)
            expected(parser, "quoted item or }");
        consume_kind(parser, CBS_TOKEN_RBRACE, "}");
    }
    body = cbs_node_create(CBS_NODE_LIST, keyword->location);
    parse_operation_block(parser, body, diagnostic_only);
    expansion = cbs_node_create(CBS_NODE_LIST, keyword->location);
    if (!parser->failed) {
        int has_operation = body->child_count > 1 ||
                            (body->child_count == 1 &&
                             body->children[0]->kind != CBS_NODE_ON_FAIL);
        if (!valid_item_name(name->text))
            parse_error(parser, name->location,
                        "each item name must be an identifier of letters, "
                        "digits, and underscores");
        else if (items->child_count == 0)
            parse_error(parser, keyword->location,
                        "each requires at least one quoted item");
        else if (!has_operation)
            parse_error(parser, keyword->location,
                        "each body must contain at least one operation");
        else if (binds_name(body, name->text)) {
            char message[320];
            snprintf(message, sizeof(message),
                     "each item name `%s` is also bound by a nested each; "
                     "nested names must be distinct",
                     name->text);
            parse_error(parser, name->location, message);
        } else if (snprintf(placeholder, sizeof(placeholder), "${each.%s}",
                            name->text) >= (int)sizeof(placeholder))
            parse_error(parser, name->location, "each item name is too long");
    }
    if (!parser->failed) {
        for (index = 0; index < items->child_count; ++index) {
            CbsNode *block = cbs_node_create(CBS_NODE_LIST, keyword->location);
            size_t child;
            block->name = cbs_duplicate(name->text);
            block->value = cbs_duplicate(items->children[index]->value);
            block->number = (long)index + 1;
            for (child = 0; child < body->child_count; ++child) {
                CbsNode *copy = clone_node(body->children[child]);
                bind_item(copy, name->text, block->value, &invalid_accessor);
                cbs_node_add(block, copy);
            }
            cbs_node_add(expansion, block);
        }
        if (invalid_accessor)
            parse_error(parser, name->location,
                        "each accessor must be basename, dirname, or stem");
    }
    cbs_node_destroy(items);
    cbs_node_destroy(body);
    return expansion;
}

/* Parse `stage library "NAME" into PATH`: copy one shared library out of the
 * build sandbox's library directories into the staged tree. */
static CbsNode *parse_stage(CbsParser *parser) {
    CbsToken *keyword = consume_word(parser, "stage");
    CbsToken *name;
    CbsToken *destination;
    CbsNode *node = cbs_node_create(CBS_NODE_STAGE, keyword->location);

    consume_word(parser, "library");
    name = consume_kind(parser, CBS_TOKEN_STRING, "library file name string");
    consume_word(parser, "into");
    destination = consume_path(parser);
    if (name != NULL)
        node->value = cbs_duplicate(name->text);
    if (destination != NULL)
        node->second_value = cbs_duplicate(destination->text);
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
    if (is_word(parser, "each"))
        return parse_each(parser, diagnostic_only);
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
    if (is_word(parser, "stage"))
        return parse_stage(parser);
    if (is_word(parser, "extract"))
        return parse_extract(parser);
    if (is_word(parser, "materialize"))
        return parse_materialize(parser);
    if (is_word(parser, "replace"))
        return parse_edit(parser, 0);
    if (is_word(parser, "insert"))
        return parse_edit(parser, 1);
    if (is_word(parser, "truncate"))
        return parse_truncate(parser);
    if (is_word(parser, "glob"))
        return parse_glob_binding(parser);
    if (is_word(parser, "links"))
        return parse_links(parser);
    if (is_word(parser, "patch"))
        return parse_patch(parser);
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
    if (is_word(parser, "license")) {
        advance(parser);
        value = consume_kind(parser, CBS_TOKEN_STRING, "license string");
        return value == NULL ? NULL : node_from_token(CBS_NODE_LICENSE, value);
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
