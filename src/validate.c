#include "cbs.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *path;
    const char *source;
    const CbsNode *package;
    int errors;
} Validator;

static void validation_error(Validator *validator, const CbsNode *node,
                             const char *code, const char *message)
{
    cbs_diagnostic(validator->path, validator->source, node->location, "error",
                   code, CBS_DIAG_VALIDATION, message);
    ++validator->errors;
}

static int valid_package_name(const char *name)
{
    const unsigned char *cursor = (const unsigned char *)name;

    if (*cursor == '\0' ||
        !(islower(*cursor) || isdigit(*cursor)))
        return 0;
    while (*++cursor != '\0') {
        if (!(islower(*cursor) || isdigit(*cursor) || *cursor == '+' ||
              *cursor == '.' || *cursor == '-'))
            return 0;
    }
    return strcmp(name, ".") != 0 && strcmp(name, "..") != 0;
}

static int valid_environment_name(const char *name)
{
    const unsigned char *cursor = (const unsigned char *)name;

    if (!(*cursor == '_' || (*cursor >= 'A' && *cursor <= 'Z')))
        return 0;
    while (*++cursor != '\0') {
        if (!(*cursor == '_' || (*cursor >= 'A' && *cursor <= 'Z') ||
              isdigit(*cursor)))
            return 0;
    }
    return 1;
}

static int valid_sha256(const char *hash)
{
    size_t index;

    if (strlen(hash) != 64)
        return 0;
    for (index = 0; index < 64; ++index) {
        if (!isdigit((unsigned char)hash[index]) &&
            !(hash[index] >= 'a' && hash[index] <= 'f'))
            return 0;
    }
    return 1;
}

static int source_declared(const Validator *validator, const char *name)
{
    size_t index;
    size_t source_index;

    for (index = 0; index < validator->package->child_count; ++index) {
        const CbsNode *sources = validator->package->children[index];
        if (sources->kind != CBS_NODE_SOURCES)
            continue;
        for (source_index = 0; source_index < sources->child_count; ++source_index) {
            const CbsNode *source = sources->children[source_index];
            if (source->value != NULL && strcmp(source->value, name) == 0)
                return 1;
        }
    }
    return 0;
}

static int known_value_name(Validator *validator, const char *name,
                            size_t length)
{
    static const char *const values[] = {
        "name", "version", "release", "arch", "src", "build", "dest", "jobs"
    };
    size_t index;

    for (index = 0; index < sizeof(values) / sizeof(values[0]); ++index) {
        if (strlen(values[index]) == length &&
            strncmp(values[index], name, length) == 0)
            return 1;
    }
    if (length > 7 && strncmp(name, "source.", 7) == 0) {
        char *source_name = cbs_duplicate_range(name + 7, length - 7);
        int declared = source_declared(validator, source_name);
        free(source_name);
        return declared;
    }
    return 0;
}

static void validate_interpolation(Validator *validator, const CbsNode *node,
                                   const char *value)
{
    const char *cursor = value;

    if (value == NULL)
        return;
    while ((cursor = strstr(cursor, "${")) != NULL) {
        const char *end = strchr(cursor + 2, '}');
        if (end == NULL) {
            validation_error(validator, node, "CPDL-E3005",
                             "unterminated CBS interpolation");
            return;
        }
        if (!known_value_name(validator, cursor + 2,
                              (size_t)(end - (cursor + 2)))) {
            validation_error(validator, node, "CPDL-E3005",
                             "unknown CBS interpolation value");
        }
        cursor = end + 1;
    }
}

static void validate_bare_value(Validator *validator, const CbsNode *node,
                                const char *value)
{
    const char *name;

    if (value == NULL || value[0] != '$' || value[1] == '{')
        return;
    name = value + 1;
    if (!known_value_name(validator, name, strlen(name)))
        validation_error(validator, node, "CPDL-E3005",
                         "unknown CBS value");
}

static void validate_value(Validator *validator, const CbsNode *node,
                           const char *value)
{
    validate_bare_value(validator, node, value);
    validate_interpolation(validator, node, value);
}

static int forbidden_executable(const char *value)
{
    static const char *const forbidden[] = {
        "sh", "bash", "dash", "ash", "ksh", "zsh", "env"
    };
    const char *base = strrchr(value, '/');
    size_t index;

    base = base == NULL ? value : base + 1;
    for (index = 0; index < sizeof(forbidden) / sizeof(forbidden[0]); ++index) {
        if (strcmp(base, forbidden[index]) == 0)
            return 1;
    }
    return 0;
}

static int valid_mode(const char *mode)
{
    size_t index;
    size_t length;

    if (mode == NULL)
        return 1;
    length = strlen(mode);
    if ((length != 4 && length != 5) || mode[0] != '0')
        return 0;
    for (index = 1; index < length; ++index) {
        if (mode[index] < '0' || mode[index] > '7')
            return 0;
    }
    return 1;
}

static int valid_glob(const char *glob)
{
    size_t index;

    for (index = 0; glob[index] != '\0'; ++index) {
        if (glob[index] == '\\') {
            if (glob[++index] == '\0')
                return 0;
        } else if (glob[index] == '[') {
            size_t start;
            ++index;
            if (glob[index] == '!')
                ++index;
            start = index;
            while (glob[index] != '\0' && glob[index] != ']')
                ++index;
            if (glob[index] != ']' || index == start)
                return 0;
        }
    }
    return 1;
}

static void duplicate_option(Validator *validator, const CbsNode *node,
                             int *seen, const char *name)
{
    char message[256];

    if (*seen) {
        snprintf(message, sizeof(message), "duplicate `%s` option", name);
        validation_error(validator, node, "CPDL-E3002", message);
    }
    *seen = 1;
}

static void validate_run(Validator *validator, const CbsNode *run,
                         int in_on_fail)
{
    int jobs = 0;
    int timeout = 0;
    int expect = 0;
    int allow_failure = 0;
    size_t index;
    size_t other;

    validate_value(validator, run, run->value);
    if (run->value != NULL && forbidden_executable(run->value))
        validation_error(validator, run, "CPDL-E3006",
                         "command interpreters are not valid run executables");
    for (index = 0; index < run->child_count; ++index) {
        const CbsNode *item = run->children[index];
        switch (item->kind) {
        case CBS_NODE_ARGUMENT:
            validate_value(validator, item, item->value);
            break;
        case CBS_NODE_RUN_ENV:
            if (!valid_environment_name(item->name))
                validation_error(validator, item, "CPDL-E3004",
                                 "invalid environment name");
            validate_value(validator, item, item->value);
            for (other = 0; other < index; ++other) {
                const CbsNode *prior = run->children[other];
                if (prior->kind == CBS_NODE_RUN_ENV && prior->name != NULL &&
                    item->name != NULL && strcmp(prior->name, item->name) == 0)
                    validation_error(validator, item, "CPDL-E3002",
                                     "duplicate command-local environment name");
            }
            break;
        case CBS_NODE_RUN_JOBS:
            duplicate_option(validator, item, &jobs, "jobs");
            if (item->value != NULL && strcmp(item->value, "$jobs") != 0)
                validation_error(validator, item, "CPDL-E3005",
                                 "jobs value must be a positive integer or $jobs");
            if (item->value == NULL && item->number <= 0)
                validation_error(validator, item, "CPDL-E3004",
                                 "jobs value must be positive");
            break;
        case CBS_NODE_RUN_TIMEOUT:
            duplicate_option(validator, item, &timeout, "timeout");
            if (item->value != NULL && item->value[0] == '0')
                validation_error(validator, item, "CPDL-E3004",
                                 "timeout must be greater than zero");
            break;
        case CBS_NODE_RUN_EXPECT:
            duplicate_option(validator, item, &expect, "expect");
            if (item->number < 0 || item->number > 255)
                validation_error(validator, item, "CPDL-E3004",
                                 "expected exit status must be between 0 and 255");
            break;
        case CBS_NODE_ALLOW_FAILURE:
            duplicate_option(validator, item, &allow_failure, "allow_failure");
            if (!in_on_fail)
                validation_error(validator, item, "CPDL-E3006",
                                 "allow_failure is valid only inside on_fail");
            break;
        default:
            validation_error(validator, item, "CPDL-E9001",
                             "invalid run AST node");
            break;
        }
    }
}

static void validate_operation(Validator *validator, const CbsNode *operation,
                               int in_on_fail);

static void validate_block(Validator *validator, const CbsNode *owner,
                           int in_on_fail)
{
    size_t index;
    int seen_on_fail = 0;

    for (index = 0; index < owner->child_count; ++index) {
        const CbsNode *operation = owner->children[index];
        if (operation->kind == CBS_NODE_ON_FAIL) {
            if (seen_on_fail)
                validation_error(validator, operation, "CPDL-E3002",
                                 "duplicate on_fail block");
            if (index + 1 != owner->child_count)
                validation_error(validator, operation, "CPDL-E3003",
                                 "on_fail must be the final block member");
            seen_on_fail = 1;
            validate_block(validator, operation, 1);
        } else {
            validate_operation(validator, operation, in_on_fail);
        }
    }
}

static void validate_selector(Validator *validator, const CbsNode *operation)
{
    validate_value(validator, operation, operation->value);
    if (operation->flag && operation->value != NULL &&
        !valid_glob(operation->value))
        validation_error(validator, operation, "CPDL-E3004",
                         "invalid glob expression");
}

static void validate_operation(Validator *validator, const CbsNode *operation,
                               int in_on_fail)
{
    size_t index;
    const char *source_name;

    if (in_on_fail && operation->kind != CBS_NODE_RUN &&
        operation->kind != CBS_NODE_REQUIRE) {
        validation_error(validator, operation, "CPDL-E3006",
                         "only run and require are valid inside on_fail");
        return;
    }
    switch (operation->kind) {
    case CBS_NODE_RUN:
        validate_run(validator, operation, in_on_fail);
        break;
    case CBS_NODE_ENV:
    case CBS_NODE_RUN_ENV:
        if (!valid_environment_name(operation->name))
            validation_error(validator, operation, "CPDL-E3004",
                             "invalid environment name");
        validate_value(validator, operation, operation->value);
        break;
    case CBS_NODE_CD:
        validate_value(validator, operation, operation->value);
        validate_block(validator, operation, in_on_fail);
        break;
    case CBS_NODE_MKDIR:
    case CBS_NODE_WRITE:
        validate_value(validator, operation, operation->value);
        validate_value(validator, operation, operation->second_value);
        if (!valid_mode(operation->second_value) && operation->kind == CBS_NODE_MKDIR)
            validation_error(validator, operation, "CPDL-E3004",
                             "invalid permission mode");
        if (operation->kind == CBS_NODE_WRITE) {
            for (index = 0; index < operation->child_count; ++index) {
                const CbsNode *property = operation->children[index];
                if (property->name != NULL && strcmp(property->name, "chmod") == 0 &&
                    !valid_mode(property->value))
                    validation_error(validator, property, "CPDL-E3004",
                                     "invalid permission mode");
            }
        }
        break;
    case CBS_NODE_COPY:
    case CBS_NODE_MOVE:
        validate_selector(validator, operation);
        validate_value(validator, operation, operation->second_value);
        break;
    case CBS_NODE_REMOVE:
    case CBS_NODE_CHMOD:
        validate_selector(validator, operation);
        if (operation->kind == CBS_NODE_CHMOD && !valid_mode(operation->second_value))
            validation_error(validator, operation, "CPDL-E3004",
                             "invalid permission mode");
        break;
    case CBS_NODE_SYMLINK:
        validate_value(validator, operation, operation->value);
        validate_value(validator, operation, operation->second_value);
        break;
    case CBS_NODE_EXTRACT:
        if (operation->value == NULL || strncmp(operation->value, "$source.", 8) != 0)
            validation_error(validator, operation, "CPDL-E3005",
                             "extract requires a named source value");
        else {
            source_name = operation->value + 8;
            if (!source_declared(validator, source_name))
                validation_error(validator, operation, "CPDL-E3005",
                                 "extract source is not declared");
        }
        validate_value(validator, operation, operation->second_value);
        break;
    case CBS_NODE_REPLACE:
    case CBS_NODE_INSERT:
        validate_value(validator, operation, operation->name);
        validate_value(validator, operation, operation->value);
        validate_value(validator, operation, operation->second_value);
        if (operation->value == NULL || operation->value[0] == '\0')
            validation_error(validator, operation, "CPDL-E3004",
                             "source-edit match value must not be empty");
        break;
    case CBS_NODE_REQUIRE:
        if (operation->name == NULL ||
            (strcmp(operation->name, "file") != 0 &&
             strcmp(operation->name, "directory") != 0 &&
             strcmp(operation->name, "glob") != 0)) {
            validation_error(validator, operation, "CPDL-E3004",
                             "require kind must be file, directory, or glob");
        }
        validate_value(validator, operation, operation->value);
        if (operation->name != NULL && strcmp(operation->name, "glob") == 0 &&
            operation->value != NULL && !valid_glob(operation->value))
            validation_error(validator, operation, "CPDL-E3004",
                             "invalid glob expression");
        for (index = 0; index < operation->child_count; ++index)
            validate_value(validator, operation->children[index],
                           operation->children[index]->value);
        break;
    default:
        validation_error(validator, operation, "CPDL-E9001",
                         "invalid operation AST node");
        break;
    }
}

static int package_item_rank(CbsNodeKind kind, const char *name)
{
    switch (kind) {
    case CBS_NODE_VERSION:
        return 1;
    case CBS_NODE_RELEASE:
        return 2;
    case CBS_NODE_ARCHITECTURE:
        return 3;
    case CBS_NODE_SOURCES:
        return 4;
    case CBS_NODE_REQUIRES:
        return 5;
    case CBS_NODE_PHASE:
        if (strcmp(name, "prepare") == 0)
            return 6;
        if (strcmp(name, "configure") == 0)
            return 7;
        if (strcmp(name, "build") == 0)
            return 8;
        if (strcmp(name, "check") == 0)
            return 9;
        return 10;
    default:
        return 99;
    }
}

static void validate_sources(Validator *validator, const CbsNode *sources)
{
    size_t index;
    size_t prior;
    int main_count = 0;

    if (sources->child_count == 0)
        validation_error(validator, sources, "CPDL-E3001",
                         "sources block must not be empty");
    for (index = 0; index < sources->child_count; ++index) {
        const CbsNode *source = sources->children[index];
        if (strcmp(source->name, "main") == 0)
            ++main_count;
        if (!valid_package_name(source->value))
            validation_error(validator, source, "CPDL-E3004",
                             "invalid source name");
        for (prior = 0; prior < index; ++prior) {
            if (strcmp(sources->children[prior]->value, source->value) == 0)
                validation_error(validator, source, "CPDL-E3002",
                                 "duplicate source name");
        }
        if (source->child_count == 2) {
            const CbsNode *url = source->children[0];
            const CbsNode *hash = source->children[1];
            if (url->value == NULL || strstr(url->value, "://") == NULL)
                validation_error(validator, url, "CPDL-E3004",
                                 "source URL must be absolute");
            if (hash->value == NULL || !valid_sha256(hash->value))
                validation_error(validator, hash, "CPDL-E3004",
                                 "SHA-256 must be 64 lowercase hexadecimal digits");
        }
    }
    if (main_count != 1)
        validation_error(validator, sources, "CPDL-E3001",
                         "sources block must contain exactly one main source");
}

static int dependency_role_rank(const char *role)
{
    if (strcmp(role, "build") == 0)
        return 1;
    if (strcmp(role, "runtime") == 0)
        return 2;
    if (strcmp(role, "test") == 0)
        return 3;
    return 4;
}

static void validate_requires(Validator *validator, const CbsNode *requires)
{
    size_t index;
    size_t prior;
    int last_rank = 0;

    for (index = 0; index < requires->child_count; ++index) {
        const CbsNode *group = requires->children[index];
        int rank = dependency_role_rank(group->name);
        if (rank < last_rank)
            validation_error(validator, group, "CPDL-E3003",
                             "dependency group appears out of order");
        last_rank = rank;
        for (prior = 0; prior < index; ++prior) {
            if (strcmp(requires->children[prior]->name, group->name) == 0)
                validation_error(validator, group, "CPDL-E3002",
                                 "duplicate dependency group");
        }
        for (prior = 0; prior < group->child_count; ++prior) {
            const CbsNode *dependency = group->children[prior];
            size_t earlier;
            if (!valid_package_name(dependency->value))
                validation_error(validator, dependency, "CPDL-E3004",
                                 "invalid dependency name");
            if (strcmp(dependency->name, "compiler") == 0 &&
                strcmp(dependency->value, "tcc") != 0)
                validation_error(validator, dependency, "CPDL-E3004",
                                 "TCC is the only permitted compiler dependency");
            for (earlier = 0; earlier < prior; ++earlier) {
                const CbsNode *other = group->children[earlier];
                if (strcmp(other->name, dependency->name) == 0 &&
                    strcmp(other->value, dependency->value) == 0)
                    validation_error(validator, dependency, "CPDL-E3002",
                                     "duplicate dependency");
            }
        }
    }
}

static void validate_package(Validator *validator)
{
    const CbsNode *package = validator->package;
    size_t index;
    size_t prior;
    int last_rank = 0;
    int versions = 0;
    int releases = 0;

    if (!valid_package_name(package->value))
        validation_error(validator, package, "CPDL-E3004",
                         "invalid package name");
    for (index = 0; index < package->child_count; ++index) {
        const CbsNode *item = package->children[index];
        int rank = package_item_rank(item->kind, item->name);
        if (rank < last_rank)
            validation_error(validator, item, "CPDL-E3003",
                             "package declaration appears out of order");
        last_rank = rank;
        for (prior = 0; prior < index; ++prior) {
            const CbsNode *other = package->children[prior];
            if (other->kind == item->kind &&
                (item->kind != CBS_NODE_PHASE ||
                 strcmp(other->name, item->name) == 0))
                validation_error(validator, item, "CPDL-E3002",
                                 "duplicate package declaration");
        }
        switch (item->kind) {
        case CBS_NODE_VERSION:
            ++versions;
            if (item->value == NULL || item->value[0] == '\0' ||
                strchr(item->value, '/') != NULL ||
                strpbrk(item->value, " \t\r\n") != NULL)
                validation_error(validator, item, "CPDL-E3004",
                                 "invalid package version");
            break;
        case CBS_NODE_RELEASE:
            ++releases;
            if (item->number <= 0)
                validation_error(validator, item, "CPDL-E3004",
                                 "release must be greater than zero");
            break;
        case CBS_NODE_ARCHITECTURE:
            if (item->value == NULL || item->value[0] == '\0')
                validation_error(validator, item, "CPDL-E3004",
                                 "architecture must not be empty");
            break;
        case CBS_NODE_SOURCES:
            validate_sources(validator, item);
            break;
        case CBS_NODE_REQUIRES:
            validate_requires(validator, item);
            break;
        case CBS_NODE_PHASE:
            validate_block(validator, item, 0);
            break;
        default:
            validation_error(validator, item, "CPDL-E9001",
                             "invalid package AST node");
            break;
        }
    }
    if (versions != 1)
        validation_error(validator, package, "CPDL-E3001",
                         "package requires exactly one version declaration");
    if (releases != 1)
        validation_error(validator, package, "CPDL-E3001",
                         "package requires exactly one release declaration");
}

int cbs_validate(const CbsNode *document, const char *path,
                 const char *source)
{
    Validator validator;

    memset(&validator, 0, sizeof(validator));
    validator.path = path;
    validator.source = source;
    if (document == NULL || document->kind != CBS_NODE_DOCUMENT ||
        document->child_count != 1 ||
        document->children[0]->kind != CBS_NODE_PACKAGE) {
        CbsLocation location;
        memset(&location, 0, sizeof(location));
        location.path = path;
        location.line = 1;
        location.column = 1;
        cbs_diagnostic(path, source, location, "error", "CPDL-E9001",
                       CBS_DIAG_INTERNAL, "invalid document AST");
        return 0;
    }
    validator.package = document->children[0];
    validate_package(&validator);
    return validator.errors == 0;
}
