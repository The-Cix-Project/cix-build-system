/* Semantic validation of the parsed CPDL document. */
#include "cbs.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    /* Recipe path used for validation diagnostics. */
    const char *path;
    /* Recipe source used to print diagnostic context. */
    const char *source;
    /* Package node currently being validated. */
    const CbsNode *package;
    /* Number of semantic errors already emitted. */
    int errors;
} Validator;

/* Emit one located validation error and increment the error count. */
static void validation_error(Validator *validator, const CbsNode *node,
                             const char *code, const char *message) {
    cbs_diagnostic(validator->path, validator->source, node->location, "error",
                   code, CBS_DIAG_VALIDATION, message);
    ++validator->errors;
}

/* Check the restricted package-name grammar. */
static int valid_package_name(const char *name) {
    const unsigned char *cursor = (const unsigned char *)name;

    if (*cursor == '\0' || !(islower(*cursor) || isdigit(*cursor)))
        return 0;
    while (*++cursor != '\0') {
        if (!(islower(*cursor) || isdigit(*cursor) || *cursor == '+' ||
              *cursor == '.' || *cursor == '-'))
            return 0;
    }
    return strcmp(name, ".") != 0 && strcmp(name, "..") != 0;
}

/* Check whether a recipe environment name is safe and portable. */
static int valid_environment_name(const char *name) {
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

/* Check that a source digest is exactly 64 hexadecimal characters. */
static int valid_sha256(const char *hash) {
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

/* Check whether a named source exists in the package declaration. */
static int source_declared(const Validator *validator, const char *name) {
    size_t index;
    size_t source_index;

    for (index = 0; index < validator->package->child_count; ++index) {
        const CbsNode *sources = validator->package->children[index];
        if (sources->kind != CBS_NODE_SOURCES)
            continue;
        for (source_index = 0; source_index < sources->child_count;
             ++source_index) {
            const CbsNode *source = sources->children[source_index];
            if (source->value != NULL && strcmp(source->value, name) == 0)
                return 1;
        }
    }
    return 0;
}

/* Check whether an interpolation name is defined by CBS policy. */
static int known_value_name(Validator *validator, const char *name,
                            size_t length) {
    static const char *const values[] = {"name",  "version", "release",
                                         "arch",  "triplet", "src",
                                         "build", "dest",    "jobs"};
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

/* Validate every interpolation embedded in a node value. */
static void validate_interpolation(Validator *validator, const CbsNode *node,
                                   const char *value) {
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

/* Validate a value that must not contain an interpolation. */
static void validate_bare_value(Validator *validator, const CbsNode *node,
                                const char *value) {
    const char *name;

    if (value == NULL || value[0] != '$' || value[1] == '\0' || value[1] == '{')
        return;
    name = value + 1;
    if (!known_value_name(validator, name, strlen(name)))
        validation_error(validator, node, "CPDL-E3005", "unknown CBS value");
}

/* Validate a normal CPDL string value and its interpolation rules. */
static void validate_value(Validator *validator, const CbsNode *node,
                           const char *value) {
    validate_bare_value(validator, node, value);
    validate_interpolation(validator, node, value);
}

/* Check an octal mode and reject privileged bits. */
static int valid_mode(const char *mode) {
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

/* Check that a glob uses only the supported pattern vocabulary. */
static int valid_glob(const char *glob) {
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

/* Report an option repeated inside one operation. */
static void duplicate_option(Validator *validator, const CbsNode *node,
                             int *seen, const char *name) {
    char message[256];

    if (*seen) {
        snprintf(message, sizeof(message), "duplicate `%s` option", name);
        validation_error(validator, node, "CPDL-E3002", message);
    }
    *seen = 1;
}

/* Validate command, argument, environment, and limit options. */
static void validate_run(Validator *validator, const CbsNode *run,
                         int in_on_fail) {
    int jobs = 0;
    int timeout = 0;
    int expect = 0;
    int allow_failure = 0;
    size_t index;
    size_t other;

    validate_value(validator, run, run->value);
    if (run->value != NULL && (cbs_is_forbidden_executable(run->value) ||
                               cbs_is_forbidden_compiler(run->value)))
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
                    validation_error(
                        validator, item, "CPDL-E3002",
                        "duplicate command-local environment name");
            }
            break;
        case CBS_NODE_RUN_JOBS:
            duplicate_option(validator, item, &jobs, "jobs");
            if (item->value != NULL && strcmp(item->value, "$jobs") != 0)
                validation_error(
                    validator, item, "CPDL-E3005",
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
                validation_error(
                    validator, item, "CPDL-E3004",
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

/* Forward-declare operation validation for recursive block validation. */
static void validate_operation(Validator *validator, const CbsNode *operation,
                               int in_on_fail);

/* Validate every operation and on-failure block in one phase. */
static void validate_block(Validator *validator, const CbsNode *owner,
                           int in_on_fail) {
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

/* Validate a selector attached to a filesystem operation. */
static void validate_selector(Validator *validator, const CbsNode *operation) {
    validate_value(validator, operation, operation->value);
    if (operation->flag && operation->value != NULL &&
        !valid_glob(operation->value))
        validation_error(validator, operation, "CPDL-E3004",
                         "invalid glob expression");
}

/* Validate one operation and its operation-specific children. */
static void validate_operation(Validator *validator, const CbsNode *operation,
                               int in_on_fail) {
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
        if (!valid_mode(operation->second_value) &&
            operation->kind == CBS_NODE_MKDIR)
            validation_error(validator, operation, "CPDL-E3004",
                             "invalid permission mode");
        if (operation->kind == CBS_NODE_WRITE) {
            for (index = 0; index < operation->child_count; ++index) {
                const CbsNode *property = operation->children[index];
                if (property->name != NULL &&
                    strcmp(property->name, "chmod") == 0 &&
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
        if (operation->kind == CBS_NODE_CHMOD &&
            !valid_mode(operation->second_value))
            validation_error(validator, operation, "CPDL-E3004",
                             "invalid permission mode");
        break;
    case CBS_NODE_SYMLINK:
        validate_value(validator, operation, operation->value);
        validate_value(validator, operation, operation->second_value);
        break;
    case CBS_NODE_EXTRACT:
        if (operation->value == NULL ||
            strncmp(operation->value, "$source.", 8) != 0)
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
    case CBS_NODE_MATERIALIZE:
        if (operation->value == NULL ||
            strncmp(operation->value, "$source.", 8) != 0)
            validation_error(validator, operation, "CPDL-E3005",
                             "materialize requires a named source value");
        else if (!source_declared(validator, operation->value + 8))
            validation_error(validator, operation, "CPDL-E3005",
                             "materialize source is not declared");
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
             strcmp(operation->name, "glob") != 0 &&
             strcmp(operation->name, "config") != 0)) {
            validation_error(
                validator, operation, "CPDL-E3004",
                "require kind must be file, directory, glob, or config");
        }
        validate_value(validator, operation, operation->value);
        if (operation->name != NULL && strcmp(operation->name, "glob") == 0 &&
            operation->value != NULL && !valid_glob(operation->value))
            validation_error(validator, operation, "CPDL-E3004",
                             "invalid glob expression");
        for (index = 0; index < operation->child_count; ++index) {
            const CbsNode *property = operation->children[index];
            if (operation->name != NULL &&
                strcmp(operation->name, "config") == 0) {
                if (property->name == NULL ||
                    strncmp(property->name, "CONFIG_", 7) != 0 ||
                    property->name[7] == '\0' ||
                    (strcmp(property->value, "y") != 0 &&
                     strcmp(property->value, "m") != 0 &&
                     strcmp(property->value, "n") != 0 &&
                     strcmp(property->value, "absent") != 0))
                    validation_error(validator, property, "CPDL-E3004",
                                     "config assertion must use CONFIG_* = y, "
                                     "m, n, or absent");
            } else {
                if (property->name == NULL ||
                    (strcmp(property->name, "contains") != 0 &&
                     strcmp(property->name, "same_as") != 0))
                    validation_error(
                        validator, property, "CPDL-E3004",
                        "file assertion must use contains or same_as");
                validate_value(validator, property, property->value);
            }
        }
        break;
    default:
        validation_error(validator, operation, "CPDL-E9001",
                         "invalid operation AST node");
        break;
    }
}

/* Return the canonical ordering rank for a package item. */
static int package_item_rank(CbsNodeKind kind, const char *name) {
    switch (kind) {
    case CBS_NODE_VERSION:
        return 1;
    case CBS_NODE_RELEASE:
        return 2;
    case CBS_NODE_UPSTREAM:
        return 3;
    case CBS_NODE_ARCHITECTURE:
        return 4;
    case CBS_NODE_SOURCES:
        return 4;
    case CBS_NODE_REQUIRES:
        return 5;
    case CBS_NODE_BUILD_IMAGE:
    case CBS_NODE_CAPABILITY:
    case CBS_NODE_TOOLCHAIN:
        return 6;
    case CBS_NODE_PHASE:
        if (strcmp(name, "prepare") == 0)
            return 7;
        if (strcmp(name, "configure") == 0)
            return 8;
        if (strcmp(name, "build") == 0)
            return 9;
        if (strcmp(name, "check") == 0)
            return 10;
        return 11;
    default:
        return 99;
    }
}

/* Validate source names, URLs, digests, and duplicate declarations. */
static void validate_sources(Validator *validator, const CbsNode *sources) {
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
        if (source->child_count >= 2) {
            const CbsNode *hash = source->children[source->child_count - 1];
            size_t url_index;
            for (url_index = 0; url_index + 1 < source->child_count;
                 ++url_index) {
                const CbsNode *url = source->children[url_index];
                if (url->kind != CBS_NODE_URL || url->value == NULL ||
                    strstr(url->value, "://") == NULL)
                    validation_error(validator, url, "CPDL-E3004",
                                     "source URL must be absolute");
            }
            if (hash->kind != CBS_NODE_SHA256 || hash->value == NULL ||
                !valid_sha256(hash->value))
                validation_error(
                    validator, hash, "CPDL-E3004",
                    "SHA-256 must be 64 lowercase hexadecimal digits");
        }
    }
    if (main_count != 1)
        validation_error(validator, sources, "CPDL-E3001",
                         "sources block must contain exactly one main source");
}

/* Return the canonical ordering rank for a dependency role. */
static int dependency_role_rank(const char *role) {
    if (strcmp(role, "build") == 0)
        return 1;
    if (strcmp(role, "runtime") == 0)
        return 2;
    if (strcmp(role, "test") == 0)
        return 3;
    return 4;
}

/* Check whether a non-default compiler is explicitly approved. */
static int has_toolchain_exception(const Validator *validator,
                                   const char *compiler) {
    size_t index;

    if (strcmp(compiler, "gcc") != 0)
        return 0;
    for (index = 0; index < validator->package->child_count; ++index)
        if (validator->package->children[index]->kind == CBS_NODE_TOOLCHAIN &&
            validator->package->children[index]->value != NULL &&
            strcmp(validator->package->children[index]->value, "gcc") == 0)
            return 1;
    return 0;
}

/* Validate dependency groups, names, roles, and ordering. */
static void validate_requires(Validator *validator, const CbsNode *requires) {
    size_t index;
    size_t prior;
    int last_rank = 0;

    for (index = 0; index < requires->child_count; ++index) {
        const CbsNode *group =
            requires
            ->children[index];
        int rank = dependency_role_rank(group->name);
        if (strcmp(group->name, "build") != 0 &&
            strcmp(group->name, "runtime") != 0 &&
            strcmp(group->name, "test") != 0 &&
            strcmp(group->name, "bootstrap") != 0)
            validation_error(validator, group, "CPDL-E3004",
                             "invalid dependency role");
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
                strcmp(dependency->value, "tcc") != 0 &&
                !has_toolchain_exception(validator, dependency->value))
                validation_error(
                    validator, dependency, "CPDL-E3004",
                    "compiler requires TCC or an explicit toolchain exception");
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

/* Validate package declarations, metadata, phases, and cardinality. */
static void validate_package(Validator *validator) {
    const CbsNode *package = validator->package;
    size_t index;
    size_t prior;
    int last_rank = 0;
    int versions = 0;
    int releases = 0;
    size_t phases = 0;

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
                item->kind != CBS_NODE_CAPABILITY &&
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
            validation_error(validator, item, "CPDL-E3006",
                             "architecture is supplied by CBS in CPDL 0.1");
            break;
        case CBS_NODE_SOURCES:
            validate_sources(validator, item);
            break;
        case CBS_NODE_REQUIRES:
            validate_requires(validator, item);
            break;
        case CBS_NODE_BUILD_IMAGE:
            if (item->value == NULL || item->value[0] == '\0')
                validation_error(validator, item, "CPDL-E3004",
                                 "build image name must not be empty");
            break;
        case CBS_NODE_CAPABILITY:
            if (item->value == NULL || item->value[0] == '\0')
                validation_error(validator, item, "CPDL-E3004",
                                 "build capability must not be empty");
            break;
        case CBS_NODE_TOOLCHAIN:
            if (item->value == NULL || strcmp(item->value, "gcc") != 0 ||
                item->child_count != 1 || item->children[0]->value == NULL ||
                item->children[0]->value[0] == '\0')
                validation_error(
                    validator, item, "CPDL-E3006",
                    "gcc toolchain use requires an explicit reason");
            break;
        case CBS_NODE_UPSTREAM:
            if (item->value == NULL || strcmp(item->value, "kernel.org") != 0)
                validation_error(validator, item, "CPDL-E3006",
                                 "unsupported upstream discovery provider");
            break;
        case CBS_NODE_PHASE:
            ++phases;
            if (phases > CBS_MAX_PHASES)
                validation_error(validator, item, "CPDL-E3004",
                                 "package has more than five phases");
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

/* Validate one complete CPDL document and report all semantic errors. */
int cbs_validate(const CbsNode *document, const char *path,
                 const char *source) {
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
