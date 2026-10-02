/* Command-line entry point and non-executing recipe inspection commands. */
#define _POSIX_C_SOURCE 200809L

#include "cbs.h"

#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <locale.h>
#include <langinfo.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef CBS_VERSION
#define CBS_VERSION "unknown"
#endif

/* Check the command-line recipe extension before parsing. */
static int has_cbs_extension(const char *path) {
    size_t length = strlen(path);
    return length >= 4 && strcmp(path + length - 4, ".cbs") == 0;
}

static int is_diagnostic_option(const char *argument) {
    return argument != NULL &&
           strcmp(argument, "--diagnostics=jsonl") == 0;
}

static void cli_errorf(const char *subject, const char *code,
                       const char *category, int status, const char *format,
                       ...) {
    char message[1024];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    cbs_cli_diagnostic("error", code, category, message, subject, status);
}

/* Read one recipe file without changing its bytes. */
static char *read_raw_file(const char *path, size_t *length) {
    FILE *file;
    long size;
    char *source;
    size_t read_length;

    file = fopen(path, "rb");
    if (file == NULL) {
        cli_errorf(path, "CPDL-E1001", "lex", 3,
                   "cannot read recipe; errno=%d", errno);
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        cli_errorf(path, "CPDL-E1001", "lex", 3,
                   "cannot measure recipe; errno=%d", errno);
        fclose(file);
        return NULL;
    }
    source = cbs_allocate((size_t)size + 1);
    read_length = fread(source, 1, (size_t)size, file);
    if (read_length != (size_t)size || ferror(file)) {
        cli_errorf(path, "CPDL-E1001", "lex", 3,
                   "cannot read complete recipe; errno=%d", errno);
        fclose(file);
        free(source);
        return NULL;
    }
    if (fclose(file) != 0) {
        cli_errorf(path, "CPDL-E1001", "lex", 3,
                   "cannot close recipe; errno=%d", errno);
        free(source);
        return NULL;
    }
    source[read_length] = '\0';
    *length = read_length;
    return source;
}

/* Read and normalize one recipe file for the lexer. */
static char *read_file(const char *path, size_t *length) {
    char *raw;
    char *normalized;
    size_t raw_length;
    size_t input = 0;
    size_t output = 0;

    raw = read_raw_file(path, &raw_length);
    if (raw == NULL)
        return NULL;
    normalized = cbs_allocate(raw_length + 1);
    while (input < raw_length) {
        if (raw[input] == '\r' && input + 1 < raw_length &&
            raw[input + 1] == '\n')
            ++input;
        normalized[output++] = raw[input++];
    }
    normalized[output] = '\0';
    free(raw);
    *length = output;
    return normalized;
}

typedef struct {
    const char *target;
    const char *value;
    int unset;
} ReviseRequest;

typedef struct {
    size_t start;
    size_t end;
    char *replacement;
} ReviseChange;

static int line_trimmed(const char *source, size_t start, size_t end,
                        const char **text, size_t *length) {
    while (start < end && (source[start] == ' ' || source[start] == '\t'))
        ++start;
    while (end > start && (source[end - 1] == '\r' ||
                           source[end - 1] == ' ' || source[end - 1] == '\t'))
        --end;
    *text = source + start;
    *length = end - start;
    return *length != 0;
}

static int line_brace_delta(const char *source, size_t start, size_t end) {
    size_t index;
    int quoted = 0;
    int escaped = 0;
    int delta = 0;
    for (index = start; index < end; ++index) {
        unsigned char character = (unsigned char)source[index];
        if (quoted) {
            if (escaped)
                escaped = 0;
            else if (character == '\\')
                escaped = 1;
            else if (character == '"')
                quoted = 0;
        } else if (character == '"') {
            quoted = 1;
        } else if (character == '#') {
            break;
        } else if (character == '{') {
            ++delta;
        } else if (character == '}') {
            --delta;
        }
    }
    return delta;
}

static int text_starts(const char *text, size_t length, const char *word) {
    size_t word_length = strlen(word);
    return length >= word_length && memcmp(text, word, word_length) == 0 &&
           (length == word_length || text[word_length] == ' ' ||
            text[word_length] == '\t');
}

static int quoted_value_span(const char *source, size_t start, size_t end,
                             size_t *value_start, size_t *value_end) {
    size_t index = start;
    while (index < end && source[index] != '"')
        ++index;
    if (index == end)
        return 0;
    *value_start = ++index;
    while (index < end) {
        if (source[index] == '\\') {
            index += index + 1 < end ? 2 : 1;
        } else if (source[index] == '"') {
            *value_end = index;
            return 1;
        } else {
            ++index;
        }
    }
    return 0;
}

static char *revise_escape(const char *value) {
    size_t index;
    size_t length = strlen(value);
    char *escaped = cbs_allocate(length * 2 + 1);
    size_t output = 0;
    for (index = 0; index < length; ++index) {
        if (value[index] == '\r' || value[index] == '\n') {
            free(escaped);
            return NULL;
        }
        if (value[index] == '"' || value[index] == '\\')
            escaped[output++] = '\\';
        escaped[output++] = value[index];
    }
    escaped[output] = '\0';
    return escaped;
}

static int revise_add_change(ReviseChange *changes, size_t *count,
                             size_t start, size_t end, const char *value) {
    if (*count >= 64)
        return 0;
    changes[*count].start = start;
    changes[*count].end = end;
    changes[*count].replacement = cbs_duplicate(value);
    ++*count;
    return 1;
}

static int revise_target_matches(const char *target, const char *kind,
                                 const char *role, const char *name,
                                 const char *field) {
    char expected[512];
    if (strcmp(kind, "source") == 0) {
        if (strcmp(role, "main") == 0)
            snprintf(expected, sizeof(expected), "source.main.%s", field);
        else
            snprintf(expected, sizeof(expected), "source.%s.%s.%s", role,
                     name, field);
    } else {
        snprintf(expected, sizeof(expected), "%s.%s", kind, field);
    }
    return strcmp(target, expected) == 0;
}

static int revise_apply(const char *path, const char *raw, size_t raw_length,
                        const ReviseRequest *requests, size_t request_count,
                        char **result, size_t *result_length) {
    ReviseChange changes[64] = {0};
    size_t change_count = 0;
    size_t request_index;
    size_t line_start = 0;
    size_t depth = 0;
    size_t metadata_level = 0;
    size_t metadata_close = raw_length;
    size_t metadata_insert = raw_length;
    int metadata_present = 0;
    int matched[32] = {0};
    size_t sources_level = 0;
    size_t source_level = 0;
    char source_role[32] = {0};
    char source_name[256] = {0};
    char newline[3] = "\n";
    char *output;
    size_t output_length = 0;
    size_t output_capacity = raw_length + 1;

    for (request_index = 0; request_index < request_count; ++request_index)
        output_capacity += strlen(requests[request_index].value == NULL
                                      ? ""
                                      : requests[request_index].value) * 2 + 1024;

    for (request_index = 1; request_index < raw_length; ++request_index) {
        if (raw[request_index - 1] == '\r' && raw[request_index] == '\n') {
            newline[0] = '\r', newline[1] = '\n', newline[2] = '\0';
            break;
        }
    }
    while (line_start < raw_length || (raw_length == 0 && line_start == 0)) {
        size_t line_end = line_start;
        size_t content_end;
        const char *trimmed;
        size_t trimmed_length;
        size_t before = depth;
        size_t delta;
        while (line_end < raw_length && raw[line_end] != '\n')
            ++line_end;
        content_end = line_end;
        if (content_end > line_start && raw[content_end - 1] == '\r')
            --content_end;
        line_trimmed(raw, line_start, content_end, &trimmed, &trimmed_length);

        if (metadata_level != 0 && before < metadata_level) {
            metadata_level = 0;
        }
        if (sources_level != 0 && before < sources_level) {
            sources_level = 0;
            source_level = 0;
            source_role[0] = '\0';
            source_name[0] = '\0';
        }
        if (before == 1 && text_starts(trimmed, trimmed_length, "metadata") &&
            memchr(trimmed, '{', trimmed_length) != NULL) {
            metadata_level = before + 1;
            metadata_present = 1;
        }
        if (!metadata_present && metadata_insert == raw_length && before == 1 &&
            (text_starts(trimmed, trimmed_length, "prepare") ||
             text_starts(trimmed, trimmed_length, "configure") ||
             text_starts(trimmed, trimmed_length, "build") ||
             text_starts(trimmed, trimmed_length, "check") ||
             text_starts(trimmed, trimmed_length, "install") ||
             (trimmed_length != 0 && trimmed[0] == '}')))
            metadata_insert = line_start;
        if (before == 1 && text_starts(trimmed, trimmed_length, "sources") &&
            memchr(trimmed, '{', trimmed_length) != NULL)
            sources_level = before + 1;
        if (sources_level != 0 && before == sources_level &&
            (text_starts(trimmed, trimmed_length, "main") ||
             text_starts(trimmed, trimmed_length, "extra"))) {
            size_t value_start, value_end;
            const char *role = text_starts(trimmed, trimmed_length, "main")
                                   ? "main"
                                   : "extra";
            if (quoted_value_span(trimmed, 0, trimmed_length, &value_start,
                                  &value_end)) {
                snprintf(source_role, sizeof(source_role), "%s", role);
                snprintf(source_name, sizeof(source_name), "%.*s",
                         (int)(value_end - value_start),
                         trimmed + value_start);
                source_level = before + 1;
            }
        }

        for (request_index = 0; request_index < request_count;
             ++request_index) {
            const ReviseRequest *request = &requests[request_index];
            size_t value_start, value_end;
            int quoted;
            int match = 0;
            int metadata_match = 0;
            if (before == 1 && strcmp(request->target, "version") == 0 &&
                text_starts(trimmed, trimmed_length, "version"))
                match = 1;
            else if (before == 1 && strcmp(request->target, "release") == 0 &&
                     text_starts(trimmed, trimmed_length, "release"))
                match = 1;
            else if (source_level != 0 && before == source_level &&
                     ((text_starts(trimmed, trimmed_length, "url") &&
                       revise_target_matches(request->target, "source",
                                             source_role, source_name, "url")) ||
                      (text_starts(trimmed, trimmed_length, "sha256") &&
                       revise_target_matches(request->target, "source",
                                             source_role, source_name, "sha256"))))
                match = 1;
            else if (metadata_level != 0 && before == metadata_level &&
                     strncmp(request->target, "metadata.", 9) == 0 &&
                     quoted_value_span(trimmed, 0, trimmed_length, &value_start,
                                       &value_end)) {
                size_t key_start = value_start;
                size_t key_end = value_end;
                char key[256];
                snprintf(key, sizeof(key), "%.*s", (int)(key_end - key_start),
                         trimmed + key_start);
                match = strcmp(request->target + 9, key) == 0;
                metadata_match = match;
                if (match && request->unset) {
                    matched[request_index] = 1;
                    if (!revise_add_change(changes, &change_count, line_start,
                                           line_end < raw_length ? line_end + 1
                                                                  : line_end,
                                           ""))
                        return 0;
                    continue;
                }
            }
            if (!match)
                continue;
            matched[request_index] = 1;
            if (request->unset) {
                cli_errorf(request->target, "CBS-E1001", "cli", 2,
                           "--unset is supported only for metadata targets");
                return 0;
            }
            if (metadata_match) {
                size_t key_end = value_end;
                quoted = quoted_value_span(trimmed, key_end + 1,
                                           trimmed_length, &value_start,
                                           &value_end);
            } else {
                quoted = quoted_value_span(trimmed, 0, trimmed_length,
                                           &value_start, &value_end);
            }
            if (!quoted) {
                size_t number_start = 0;
                while (number_start < trimmed_length &&
                       trimmed[number_start] != ' ' &&
                       trimmed[number_start] != '\t')
                    ++number_start;
                while (number_start < trimmed_length &&
                       (trimmed[number_start] == ' ' ||
                        trimmed[number_start] == '\t'))
                    ++number_start;
                value_start = number_start;
                value_end = trimmed_length;
            } else {
                value_start += (size_t)(trimmed - raw);
                value_end += (size_t)(trimmed - raw);
            }
            if (quoted) {
                char *escaped = revise_escape(request->value);
                if (escaped == NULL || !revise_add_change(
                                           changes, &change_count, value_start,
                                           value_end, escaped)) {
                    free(escaped);
                    cli_errorf(request->target, "CBS-E1001", "cli", 2,
                               "revision value contains a newline or is too long");
                    return 0;
                }
                free(escaped);
            } else {
                if (!revise_add_change(changes, &change_count,
                                       (size_t)(trimmed - raw) + value_start,
                                       (size_t)(trimmed - raw) + value_end,
                                       request->value))
                    return 0;
            }
        }
        delta = line_brace_delta(raw, line_start, content_end);
        if (metadata_level != 0 && before == metadata_level && delta == 0 &&
            memchr(trimmed, '}', trimmed_length) != NULL)
            metadata_close = line_start;
        if (delta >= 0)
            depth += (size_t)delta;
        else if ((size_t)(-delta) <= depth)
            depth -= (size_t)(-delta);
        else
            depth = 0;
        line_start = line_end < raw_length ? line_end + 1 : raw_length;
        if (line_end == raw_length)
            break;
    }

    for (request_index = 0; request_index < request_count; ++request_index) {
        const ReviseRequest *request = &requests[request_index];
        if (request->unset || strncmp(request->target, "metadata.", 9) != 0)
            continue;
        {
            if (!matched[request_index]) {
                char *escaped = revise_escape(request->value);
                char insertion[1024];
                if (escaped == NULL ||
                    snprintf(insertion, sizeof(insertion),
                             "    \"%s\" \"%s\"%s", request->target + 9,
                             escaped, newline) >= (int)sizeof(insertion)) {
                    free(escaped);
                    cli_errorf(request->target, "CBS-E1001", "cli", 2,
                               "metadata key or value is too long");
                    return 0;
                }
                if (metadata_close != raw_length) {
                    matched[request_index] = 1;
                    if (!revise_add_change(changes, &change_count,
                                           metadata_close, metadata_close,
                                           insertion)) {
                        free(escaped);
                        return 0;
                    }
                } else if (metadata_insert != raw_length) {
                    char block[1200];
                    matched[request_index] = 1;
                    if (snprintf(block, sizeof(block), "metadata {%s    \"%s\" \"%s\"%s}%s",
                                 newline, request->target + 9,
                                 escaped == NULL ? "" : escaped, newline,
                                 newline) >= (int)sizeof(block)) {
                        cli_errorf(request->target, "CBS-E1001", "cli", 2,
                                   "metadata key or value is too long");
                        free(escaped);
                        return 0;
                    }
                    if (!revise_add_change(changes, &change_count,
                                           metadata_insert, metadata_insert,
                                           block)) {
                        free(escaped);
                        return 0;
                    }
                } else {
                    cli_errorf(request->target, "CPDL-E3004", "validation", 3,
                               "metadata target is absent and no metadata block exists");
                    return 0;
                }
                free(escaped);
            }
        }
    }
    for (request_index = 0; request_index < request_count; ++request_index) {
        const ReviseRequest *request = &requests[request_index];
        if (!matched[request_index] && !request->unset) {
            cli_errorf(request->target, "CPDL-E3004", "validation", 3,
                       "revision target is absent or ambiguous");
            return 0;
        }
        if (!matched[request_index] && request->unset) {
            cli_errorf(request->target, "CPDL-E3004", "validation", 3,
                       "revision target is absent or ambiguous");
            return 0;
        }
    }
    output = cbs_allocate(output_capacity);
    {
        size_t cursor = 0;
        size_t index;
        for (index = 0; index < change_count; ++index) {
            size_t next;
            for (next = index + 1; next < change_count; ++next)
                if (changes[next].start < changes[index].start) {
                    ReviseChange temporary = changes[index];
                    changes[index] = changes[next];
                    changes[next] = temporary;
                }
            if (changes[index].start < cursor) {
                cli_errorf(path, "CBS-E1001", "cli", 2,
                           "revision targets overlap");
                free(output);
                return 0;
            }
            memcpy(output + output_length, raw + cursor,
                   changes[index].start - cursor);
            output_length += changes[index].start - cursor;
            strcpy(output + output_length, changes[index].replacement);
            output_length += strlen(changes[index].replacement);
            cursor = changes[index].end;
        }
        memcpy(output + output_length, raw + cursor, raw_length - cursor);
        output_length += raw_length - cursor;
    }
    output[output_length] = '\0';
    for (request_index = 0; request_index < change_count; ++request_index)
        free(changes[request_index].replacement);
    *result = output;
    *result_length = output_length;
    return 1;
}

static int validate_recipe_bytes(const char *path, const char *raw,
                                 size_t raw_length) {
    char *source = cbs_allocate(raw_length + 1);
    size_t input = 0;
    size_t output = 0;
    CbsTokenList tokens = {0};
    CbsNode *document;
    int valid;

    while (input < raw_length) {
        if (raw[input] == '\r' && input + 1 < raw_length &&
            raw[input + 1] == '\n')
            ++input;
        source[output++] = raw[input++];
    }
    source[output] = '\0';
    if (!cbs_lex(path, source, output, &tokens)) {
        cbs_token_list_destroy(&tokens);
        free(source);
        return 0;
    }
    document = cbs_parse(path, source, output, &tokens);
    valid = document != NULL && cbs_validate(document, path, source);
    cbs_node_destroy(document);
    cbs_token_list_destroy(&tokens);
    free(source);
    return valid;
}

static int write_revised_recipe(const char *path, const char *data,
                                size_t length) {
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        cli_errorf(path, "CBS-E1001", "cli", 2,
                   "cannot write revised recipe; errno=%d", errno);
        return 0;
    }
    if (fwrite(data, 1, length, file) != length) {
        fclose(file);
        cli_errorf(path, "CBS-E1001", "cli", 2,
                   "cannot write revised recipe; errno=%d", errno);
        return 0;
    }
    if (fclose(file) != 0) {
        cli_errorf(path, "CBS-E1001", "cli", 2,
                   "cannot write revised recipe; errno=%d", errno);
        return 0;
    }
    return 1;
}

static int parse_revise_request(const char *spec, int unset,
                                ReviseRequest *request) {
    const char *equals = unset ? NULL : strchr(spec, '=');
    size_t target_length = equals == NULL ? strlen(spec)
                                          : (size_t)(equals - spec);
    if (target_length == 0 || target_length >= 256 ||
        (!unset && (equals[1] == '\0' || strlen(equals + 1) >= 768)))
        return 0;
    request->target = cbs_duplicate_range(spec, target_length);
    request->value = unset ? NULL : cbs_duplicate(equals + 1);
    request->unset = unset;
    if ((strncmp(request->target, "metadata.", 9) != 0 && unset) ||
        strchr(request->target, '=') != NULL ||
        strchr(request->target, '/') != NULL) {
        free((void *)request->target);
        free((void *)request->value);
        return 0;
    }
    return 1;
}

static void free_revise_requests(ReviseRequest *requests, size_t count) {
    size_t index;
    for (index = 0; index < count; ++index) {
        free((void *)requests[index].target);
        free((void *)requests[index].value);
    }
}

static int revise_file(const char *path, ReviseRequest *requests,
                       size_t request_count, const char *output_path) {
    char *raw;
    char *revised;
    size_t raw_length;
    size_t revised_length;
    if (!has_cbs_extension(path)) {
        cli_errorf(path, "CPDL-E3004", "validation", 3,
                   "recipe must use the .cbs extension");
        return 3;
    }
    raw = read_raw_file(path, &raw_length);
    if (raw == NULL)
        return 3;
    if (!validate_recipe_bytes(path, raw, raw_length)) {
        free(raw);
        return 3;
    }
    if (!revise_apply(path, raw, raw_length, requests, request_count,
                      &revised, &revised_length)) {
        free(raw);
        return 3;
    }
    if (!validate_recipe_bytes(path, revised, revised_length)) {
        cli_errorf(path, "CPDL-E3004", "validation", 3,
                   "revision would produce an invalid recipe");
        free(raw);
        free(revised);
        return 3;
    }
    if (output_path != NULL) {
        if (!write_revised_recipe(output_path, revised, revised_length)) {
            free(raw);
            free(revised);
            return 2;
        }
    } else {
        if (fwrite(revised, 1, revised_length, stdout) != revised_length) {
            cli_errorf(path, "CBS-E1001", "cli", 2,
                       "cannot write revised recipe to stdout");
            free(raw);
            free(revised);
            return 2;
        }
    }
    free(raw);
    free(revised);
    return 0;
}

/* Implement the validate/check command without executing a recipe. */
static int validate_file(const char *path) {
    char *source;
    size_t length;
    CbsTokenList tokens;
    CbsNode *document;
    int valid;

    memset(&tokens, 0, sizeof(tokens));
    if (!has_cbs_extension(path)) {
        cli_errorf(path, "CPDL-E3004", "validation", 3,
                   "recipe must use the .cbs extension");
        return 3;
    }
    source = read_file(path, &length);
    if (source == NULL)
        return 3;
    if (!cbs_lex(path, source, length, &tokens)) {
        cbs_token_list_destroy(&tokens);
        free(source);
        return 3;
    }
    document = cbs_parse(path, source, length, &tokens);
    if (document == NULL) {
        cbs_token_list_destroy(&tokens);
        free(source);
        return 3;
    }
    valid = cbs_validate(document, path, source);
    cbs_node_destroy(document);
    cbs_token_list_destroy(&tokens);
    free(source);
    if (!valid)
        return 3;
    printf("%s: valid CPDL 1.0\n", path);
    return 0;
}

/* Report extraction failures against the object that actually failed. */
static int extract_file(const char *artifact, const char *destination) {
    char parent[4096];
    char *slash;
    struct stat status;

    if (destination == NULL || destination[0] == '\0' ||
        strlen(destination) > sizeof(parent) - 32) {
        cli_errorf(destination == NULL ? "(null)" : destination,
                   "CIXPKG-E4002", "artifact", 4,
                   "invalid extraction destination");
        return 4;
    }
    if (lstat(destination, &status) == 0) {
        cli_errorf(destination, "CIXPKG-E4003", "artifact", 4,
                   "extraction destination already exists");
        return 4;
    }
    if (errno != ENOENT) {
        cli_errorf(destination, "CIXPKG-E4002", "artifact", 4,
                   "cannot inspect extraction destination; errno=%d", errno);
        return 4;
    }
    if (snprintf(parent, sizeof(parent), "%s", destination) >=
        (int)sizeof(parent)) {
        cli_errorf(destination, "CIXPKG-E4002", "artifact", 4,
                   "invalid extraction destination");
        return 4;
    }
    slash = strrchr(parent, '/');
    if (slash == NULL) {
        strcpy(parent, ".");
    } else if (slash == parent) {
        parent[1] = '\0';
    } else {
        *slash = '\0';
    }
    if (stat(parent, &status) != 0) {
        if (errno == ENOENT)
            cli_errorf(destination, "CIXPKG-E4004", "artifact", 4,
                       "extraction destination parent does not exist");
        else
            cli_errorf(destination, "CIXPKG-E4002", "artifact", 4,
                       "extraction destination parent is unavailable; errno=%d",
                       errno);
        return 4;
    }
    if (!S_ISDIR(status.st_mode)) {
        cli_errorf(destination, "CIXPKG-E4002", "artifact", 4,
                   "extraction destination parent is not a directory");
        return 4;
    }
    if (!cbs_cixpkg_verify_tree(artifact, NULL, 0)) {
        cli_errorf(artifact, "CIXPKG-E4001", "artifact", 4,
                   "artifact verification failed");
        return 4;
    }
    if (!cbs_cixpkg_extract(artifact, destination)) {
        cli_errorf(destination, "CIXPKG-E4002", "artifact", 4,
                   "could not populate extraction destination");
        return 4;
    }
    printf("extracted %s\n", destination);
    return 0;
}

/* Print the command-line interface summary. */
static void usage(FILE *stream) {
    fputs(
        "usage: cbs <command> [options]\n"
        "\ncbs - Cix Build System package engine (CPDL 1.0)\n\n"
        "commands:\n"
        "  cbs check RECIPE.cbs                 Validate without executing\n"
        "  cbs validate RECIPE.cbs [--json]     Alias for check\n"
        "  cbs explain RECIPE.cbs [--json]       Show the execution plan\n"
        "  cbs revise RECIPE.cbs [--set TARGET=VALUE] [--unset TARGET]\n"
        "      [--output FILE]                    Revise without normalizing bytes\n"
        "  cbs inspect RECIPE.cbs [ARTIFACT]    Show digest metadata\n"
        "  cbs build RECIPE.cbs --arch ARCH --staged ROOT [--output FILE] "
        "[--cache DIR] [--ca-file FILE] [--events human|jsonl] "
        "[--finalize-command CMD] [--prune-policy FILE] [--firmware-root DIR]\n"
        "      [--report FILE]\n"
        "      [--input NAME=FILE ...]\n"
        "      [--command-path DIRS]\n"
        "      [--library-path DIRS]\n"
        "      [--tool-identity NAME@VERSION=MANIFEST_SHA256 ...]\n"
        "  cbs package ROOT --name NAME --version VERSION --release N "
        "--arch ARCH --output FILE [--license SPDX]\n"
        "      [--diagnostics=jsonl]            Emit versioned machine diagnostics\n"
        "  cbs doctor [RECIPE.cbs] [--arch ARCH] [--staged ROOT] [--cache DIR]\n"
        "      [--command-path DIRS] [--library-path DIRS] Preflight safely\n"
        "  cbs fingerprint RECIPE.cbs --arch ARCH [--command-path DIRS]\n"
        "      [--library-path DIRS] [--tool-identity NAME@VERSION=MANIFEST_SHA256 ...]\n"
        "      Compute the build action fingerprint\n"
        "  cbs list ARTIFACT.cixpkg [--json]    List verified manifest entries\n"
        "  cbs diff LEFT.cixpkg RIGHT.cixpkg [--json] Compare manifests\n"
        "  cbs --capabilities                    Show integration capabilities\n"
        "  cbs verify ARTIFACT.cixpkg           Verify an artifact alone\n"
        "  cbs extract ARTIFACT.cixpkg --into DIR Extract a verified artifact\n"
        "  cbs --help                           Show this help\n"
        "  cbs --version                        Show version\n",
        stream);
}

typedef struct {
    const char *recipe;
    const char *architecture;
    const char *staged;
    const char *output;
    const char *cache;
    const char *ca_file;
    const char *events;
    const char *finalize_command;
    const char *prune_policy;
    const char *firmware_root;
    const char *command_path;
    const char *library_path;
    const char *report_path;
    CbsInputBinding inputs[32];
    size_t input_count;
    CbsToolIdentity tool_identities[32];
    size_t tool_identity_count;
} CbsBuildOptions;

static int valid_input_name(const char *name) {
    size_t index;
    if (name == NULL || name[0] == '\0' ||
        !(isalpha((unsigned char)name[0]) || name[0] == '_'))
        return 0;
    for (index = 1; name[index] != '\0'; ++index)
        if (!(isalnum((unsigned char)name[index]) || name[index] == '_'))
            return 0;
    return 1;
}

static int valid_manifest_digest(const char *digest) {
    size_t index;
    if (digest == NULL || strlen(digest) != 64)
        return 0;
    for (index = 0; index < 64; ++index)
        if (!isxdigit((unsigned char)digest[index]))
            return 0;
    return 1;
}

static int add_tool_identity(CbsToolIdentity *items, size_t *count,
                             const char *spec, const char *command) {
    const char *equals = strchr(spec, '=');
    const char *at = strchr(spec, '@');
    char *name;
    char *version;
    char *digest;
    size_t index;
    if (*count == 32 || equals == NULL || at == NULL || at == spec ||
        at > equals || equals[1] == '\0' ||
        !valid_manifest_digest(equals + 1)) {
        cli_errorf(command, "CBS-E1001", "cli", 2,
                   "--tool-identity requires NAME@VERSION=64-hex-MANIFEST-SHA256");
        return 0;
    }
    name = cbs_duplicate_range(spec, (size_t)(at - spec));
    version = cbs_duplicate_range(at + 1, (size_t)(equals - at - 1));
    digest = cbs_duplicate(equals + 1);
    if (name[0] == '\0' || version[0] == '\0') {
        free(name);
        free(version);
        free(digest);
        cli_errorf(command, "CBS-E1001", "cli", 2,
                   "--tool-identity requires NAME@VERSION=64-hex-MANIFEST-SHA256");
        return 0;
    }
    for (index = 0; index < *count; ++index)
        if (strcmp(items[index].name, name) == 0 &&
            strcmp(items[index].version, version) == 0) {
            cli_errorf(command, "CBS-E1002", "cli", 2,
                       "duplicate --tool-identity `%s@%s`", name, version);
            free(name);
            free(version);
            free(digest);
            return 0;
        }
    items[*count].name = name;
    items[*count].version = version;
    items[*count].manifest_digest = digest;
    ++*count;
    return 1;
}

static void free_tool_identities(CbsToolIdentity *items, size_t count) {
    size_t index;
    for (index = 0; index < count; ++index) {
        free((void *)items[index].name);
        free((void *)items[index].version);
        free((void *)items[index].manifest_digest);
    }
}

static int add_input(CbsBuildOptions *options, const char *spec) {
    const char *separator = strchr(spec, '=');
    char *name;
    int valid;
    if (options->input_count == 32 || separator == NULL || separator == spec ||
        separator[1] == '\0' || separator[1] != '/') {
        cli_errorf("build", "CBS-E1001", "cli", 2,
                   "--input requires NAME=ABSOLUTE_FILE with a portable name");
        return 0;
    }
    name = cbs_duplicate_range(spec, (size_t)(separator - spec));
    valid = valid_input_name(name);
    if (!valid) {
        free(name);
        cli_errorf("build", "CBS-E1001", "cli", 2,
                   "--input requires NAME=ABSOLUTE_FILE with a portable name");
        return 0;
    }
    for (size_t index = 0; index < options->input_count; ++index) {
        if (strcmp(options->inputs[index].name, name) == 0) {
            cli_errorf("build", "CBS-E1002", "cli", 2,
                       "duplicate --input name `%s`", name);
            free(name);
            return 0;
        }
    }
    options->inputs[options->input_count].name = name;
    options->inputs[options->input_count].path = cbs_duplicate(separator + 1);
    ++options->input_count;
    return 1;
}

static void free_inputs(CbsBuildOptions *options) {
    size_t index;
    for (index = 0; index < options->input_count; ++index) {
        free((void *)options->inputs[index].name);
        free((void *)options->inputs[index].path);
    }
}

/* Parse build options independently of their order on the command line. */
static int parse_build_options(int argc, char **argv, CbsBuildOptions *options) {
    int index;
    memset(options, 0, sizeof(*options));
    if (argc < 3 || strcmp(argv[1], "build") != 0) {
        cli_errorf("build", "CBS-E1003", "cli", 2, "missing recipe");
        return 0;
    }
    options->recipe = argv[2];
    for (index = 3; index < argc; ++index) {
        const char *argument = argv[index];
        const char *value = NULL;
        if (is_diagnostic_option(argument))
            continue;
        if (strncmp(argument, "--input=", 8) == 0) {
            if (!add_input(options, argument + 8))
                return 0;
            continue;
        }
        if (strcmp(argument, "--input") == 0) {
            if (++index >= argc || !add_input(options, argv[index])) {
                if (index >= argc)
                    cli_errorf("build", "CBS-E1001", "cli", 2,
                               "option `--input` requires a value");
                return 0;
            }
            continue;
        }
        if (strncmp(argument, "--tool-identity=", 16) == 0) {
            if (!add_tool_identity(options->tool_identities,
                                   &options->tool_identity_count,
                                   argument + 16, "build"))
                return 0;
            continue;
        }
        if (strcmp(argument, "--tool-identity") == 0) {
            if (++index >= argc ||
                !add_tool_identity(options->tool_identities,
                                   &options->tool_identity_count, argv[index],
                                   "build")) {
                if (index >= argc)
                    cli_errorf("build", "CBS-E1001", "cli", 2,
                               "option `--tool-identity` requires a value");
                return 0;
            }
            continue;
        }
        if (strncmp(argument, "--arch=", 7) == 0)
            value = argument + 7;
        else if (strncmp(argument, "--staged=", 9) == 0)
            value = argument + 9;
        else if (strncmp(argument, "--output=", 9) == 0)
            value = argument + 9;
        else if (strncmp(argument, "--cache=", 8) == 0)
            value = argument + 8;
        else if (strncmp(argument, "--ca-file=", 10) == 0)
            value = argument + 10;
        else if (strncmp(argument, "--events=", 9) == 0)
            value = argument + 9;
        else if (strncmp(argument, "--finalize-command=", 19) == 0)
            value = argument + 19;
        else if (strncmp(argument, "--prune-policy=", 15) == 0)
            value = argument + 15;
        else if (strncmp(argument, "--firmware-root=", 16) == 0)
            value = argument + 16;
        else if (strncmp(argument, "--command-path=", 15) == 0)
            value = argument + 15;
        else if (strncmp(argument, "--library-path=", 15) == 0)
            value = argument + 15;
        else if (strncmp(argument, "--report=", 9) == 0)
            value = argument + 9;
        else if (strcmp(argument, "--arch") == 0 ||
                 strcmp(argument, "--staged") == 0 ||
                 strcmp(argument, "--output") == 0 ||
                 strcmp(argument, "--cache") == 0 ||
                 strcmp(argument, "--ca-file") == 0 ||
                 strcmp(argument, "--events") == 0 ||
                 strcmp(argument, "--finalize-command") == 0 ||
                 strcmp(argument, "--prune-policy") == 0 ||
                 strcmp(argument, "--firmware-root") == 0 ||
                 strcmp(argument, "--command-path") == 0 ||
                 strcmp(argument, "--report") == 0) {
            if (++index >= argc) {
                cli_errorf("build", "CBS-E1001", "cli", 2,
                           "option `%s` requires a value", argument);
                return 0;
            }
            value = argv[index];
        } else if (strcmp(argument, "--library-path") == 0) {
            if (++index >= argc) {
                cli_errorf("build", "CBS-E1001", "cli", 2,
                           "option `%s` requires a value", argument);
                return 0;
            }
            value = argv[index];
        } else {
            cli_errorf("build", "CBS-E1001", "cli", 2,
                       "unknown option `%s`", argument);
            return 0;
        }
        if (value == NULL || value[0] == '\0') {
            cli_errorf("build", "CBS-E1001", "cli", 2,
                       "option `%s` requires a non-empty value", argument);
            return 0;
        }
        if (strcmp(argument, "--arch") == 0 ||
            strncmp(argument, "--arch=", 7) == 0)
            options->architecture = value;
        else if (strcmp(argument, "--staged") == 0 ||
                 strncmp(argument, "--staged=", 9) == 0)
            options->staged = value;
        else if (strcmp(argument, "--output") == 0 ||
                 strncmp(argument, "--output=", 9) == 0)
            options->output = value;
        else if (strcmp(argument, "--cache") == 0 ||
                 strncmp(argument, "--cache=", 8) == 0)
            options->cache = value;
        else if (strcmp(argument, "--ca-file") == 0 ||
                 strncmp(argument, "--ca-file=", 10) == 0)
            options->ca_file = value;
        else if (strcmp(argument, "--finalize-command") == 0 ||
                 strncmp(argument, "--finalize-command=", 19) == 0)
            options->finalize_command = value;
        else if (strcmp(argument, "--prune-policy") == 0 ||
                 strncmp(argument, "--prune-policy=", 15) == 0)
            options->prune_policy = value;
        else if (strcmp(argument, "--firmware-root") == 0 ||
                 strncmp(argument, "--firmware-root=", 16) == 0)
            options->firmware_root = value;
        else if (strcmp(argument, "--command-path") == 0 ||
                 strncmp(argument, "--command-path=", 15) == 0)
            options->command_path = value;
        else if (strcmp(argument, "--library-path") == 0 ||
                 strncmp(argument, "--library-path=", 15) == 0)
            options->library_path = value;
        else if (strcmp(argument, "--report") == 0 ||
                 strncmp(argument, "--report=", 9) == 0)
            options->report_path = value;
        else
            options->events = value;
    }
    if (options->architecture == NULL || options->staged == NULL) {
        cli_errorf("build", "CBS-E1001", "cli", 2,
                   "--arch and --staged are required");
        return 0;
    }
    return 1;
}

/* Verify one CIXPKG artifact and print its identity. */
static int verify_file(const char *path) {
    char identity[129];
    if (!cbs_cixpkg_verify_tree(path, identity, sizeof(identity))) {
        cli_errorf(path, "CIXPKG-E4001", "artifact", 4,
                   "artifact verification failed");
        return 4;
    }
    printf("%s: verified CIXPKG (identity=%s)\n", path, identity);
    return 0;
}

/* Print a JSON string with quotes and backslashes escaped. */
static void print_json_string(const char *value) {
    const unsigned char *cursor;
    if (value == NULL) {
        fputs("null", stdout);
        return;
    }
    putchar('"');
    for (cursor = (const unsigned char *)value; *cursor != '\0'; ++cursor) {
        if (*cursor == '"' || *cursor == '\\')
            putchar('\\');
        putchar(*cursor);
    }
    putchar('"');
}

static const CbsNode *upstream_property(const CbsNode *upstream,
                                        const char *name) {
    size_t index;
    for (index = 0; index < upstream->child_count; ++index)
        if (strcmp(upstream->children[index]->name, name) == 0)
            return upstream->children[index];
    return NULL;
}

static const CbsNode *upstream_verify_field(const CbsNode *verify,
                                            const char *name) {
    size_t index;
    for (index = 0; index < verify->child_count; ++index)
        if (strcmp(verify->children[index]->name, name) == 0)
            return verify->children[index];
    return NULL;
}

static void print_upstream_json(const CbsNode *upstream) {
    const CbsNode *tag = upstream_property(upstream, "tag");
    const CbsNode *source = upstream_property(upstream, "source");
    const CbsNode *verify = upstream_property(upstream, "verify");
    fputs("{\"provider\":", stdout);
    print_json_string(upstream->value);
    fputs(",\"tag\":", stdout);
    print_json_string(tag == NULL ? NULL : tag->value);
    fputs(",\"source\":", stdout);
    print_json_string(source == NULL ? NULL : source->value);
    fputs(",\"verify\":", stdout);
    if (verify == NULL) {
        fputs("null", stdout);
    } else {
        const CbsNode *url = upstream_verify_field(verify, "url");
        const CbsNode *key = upstream_verify_field(verify, "key");
        fputs("{\"method\":", stdout);
        print_json_string(verify->value);
        if (verify->second_value != NULL) {
            fputs(",\"format\":", stdout);
            print_json_string(verify->second_value);
        }
        if (url != NULL) {
            fputs(",\"url\":", stdout);
            print_json_string(url->value);
        }
        if (key != NULL) {
            fputs(",\"key\":", stdout);
            print_json_string(key->value);
        } else if (strcmp(verify->value, "signed-tag") == 0) {
            fputs(",\"key\":", stdout);
            print_json_string(verify->second_value);
        }
        fputc('}', stdout);
    }
    fputc('}', stdout);
}

/* Count expanded operations: a list (a `for` or `each` expansion) counts
 * what it contains, recursively. */
static size_t count_plan_operations(const CbsNode *block) {
    size_t total = 0;
    size_t index;
    for (index = 0; index < block->child_count; ++index)
        total += block->children[index]->kind == CBS_NODE_LIST
                     ? count_plan_operations(block->children[index])
                     : 1;
    return total;
}

static const CbsNode *explained_tools(const CbsNode *document) {
    const CbsNode *package = document->children[0];
    size_t index;
    for (index = 0; index < package->child_count; ++index)
        if (package->children[index]->kind == CBS_NODE_TOOLS)
            return package->children[index];
    return NULL;
}

/* Validate a recipe and print its execution metadata and plan. */
static int explain_file(const char *path, int json) {
    char *source;
    size_t length, index;
    CbsTokenList tokens = {0};
    CbsNode *document;
    CbsBuildPlan plan;
    CbsBuildMetadata metadata;

    if (!has_cbs_extension(path)) {
        cli_errorf(path, "CPDL-E3004", "validation", 3,
                   "recipe must use the .cbs extension");
        return 3;
    }
    source = read_file(path, &length);
    if (source == NULL)
        return 3;
    if (!cbs_lex(path, source, length, &tokens)) {
        free(source);
        cbs_token_list_destroy(&tokens);
        return 3;
    }
    document = cbs_parse(path, source, length, &tokens);
    if (document == NULL || !cbs_validate(document, path, source) ||
        !cbs_build_plan(document, &plan)) {
        cbs_node_destroy(document);
        cbs_token_list_destroy(&tokens);
        free(source);
        return 3;
    }
    if (!cbs_build_metadata(document, &metadata)) {
        cbs_node_destroy(document);
        cbs_token_list_destroy(&tokens);
        free(source);
        return 3;
    }
    if (json) {
        const CbsNode *package = document->children[0];
        size_t item_index, child_index;
        const char *version = NULL;
        const char *format = NULL;
        long release = 0;
        fputs("{\"name\":", stdout);
        print_json_string(package->value);
        for (item_index = 0; item_index < package->child_count; ++item_index) {
            if (package->children[item_index]->kind == CBS_NODE_VERSION)
                version = package->children[item_index]->value;
            if (package->children[item_index]->kind == CBS_NODE_RELEASE)
                release = package->children[item_index]->number;
            if (package->children[item_index]->kind == CBS_NODE_FORMAT)
                format = package->children[item_index]->value;
        }
        fputs(",\"version\":", stdout);
        print_json_string(version);
        printf(",\"release\":%ld,\"format\":", release);
        print_json_string(format);
        fputs(",\"architecture\":null,\"sources\":[", stdout);
        {
            int first_source = 1;
            for (item_index = 0; item_index < package->child_count;
                 ++item_index) {
                const CbsNode *sources = package->children[item_index];
                if (sources->kind != CBS_NODE_SOURCES)
                    continue;
                for (child_index = 0; child_index < sources->child_count;
                     ++child_index) {
                    const CbsNode *source = sources->children[child_index];
                    size_t url_index;
                    if (!first_source)
                        putchar(',');
                    first_source = 0;
                    fputs("{\"name\":", stdout);
                    print_json_string(source->value);
                    fputs(",\"urls\":[", stdout);
                    {
                        int first_url = 1;
                        for (url_index = 0; url_index < source->child_count;
                             ++url_index) {
                            const CbsNode *url = source->children[url_index];
                            if (url->kind != CBS_NODE_URL)
                                continue;
                            if (!first_url)
                                putchar(',');
                            first_url = 0;
                            print_json_string(url->value);
                        }
                    }
                    fputs("],\"sha256\":", stdout);
                    for (url_index = 0; url_index < source->child_count;
                         ++url_index)
                        if (source->children[url_index]->kind ==
                            CBS_NODE_SHA256)
                            print_json_string(
                                source->children[url_index]->value);
                    fputs("}", stdout);
                }
            }
        }
        fputs("],\"requires\":{", stdout);
        {
            int first_group = 1;
            for (item_index = 0; item_index < package->child_count;
                 ++item_index) {
                const CbsNode *
                    requires
                = package->children[item_index];
                if (requires->kind != CBS_NODE_REQUIRES)
                    continue;
                for (child_index = 0; child_index < requires->child_count;
                     ++child_index) {
                    const CbsNode *group =
                        requires
                        ->children[child_index];
                    size_t dep_index;
                    if (!first_group)
                        putchar(',');
                    first_group = 0;
                    print_json_string(group->name);
                    fputs(":{", stdout);
                    for (dep_index = 0; dep_index < group->child_count;
                         ++dep_index) {
                        const CbsNode *dep = group->children[dep_index];
                        size_t later;
                        int first_value = 1;
                        for (later = 0; later < dep_index; ++later)
                            if (strcmp(group->children[later]->name,
                                       dep->name) == 0)
                                break;
                        if (later != dep_index)
                            continue;
                        if (dep_index != 0)
                            putchar(',');
                        print_json_string(dep->name);
                        putchar(':');
                        fputs("[", stdout);
                        for (later = dep_index; later < group->child_count;
                             ++later) {
                            if (strcmp(group->children[later]->name,
                                       dep->name) != 0)
                                continue;
                            if (!first_value)
                                putchar(',');
                            first_value = 0;
                            print_json_string(group->children[later]->value);
                        }
                        fputs("]", stdout);
                    }
                    fputs("}", stdout);
                }
            }
        }
        fputs("},\"replaces\":[", stdout);
        {
            int first_replacement = 1;
            for (item_index = 0; item_index < package->child_count;
                 ++item_index) {
                const CbsNode *replaces = package->children[item_index];
                if (replaces->kind != CBS_NODE_REPLACES)
                    continue;
                for (child_index = 0; child_index < replaces->child_count;
                     ++child_index) {
                    if (!first_replacement)
                        putchar(',');
                    first_replacement = 0;
                    print_json_string(replaces->children[child_index]->value);
                }
            }
        }
        fputs("],\"resources\":", stdout);
        if (metadata.memory_declared)
            printf("{\"memory\":%llu}", metadata.memory_bytes);
        else
            fputs("null", stdout);
        fputs(",\"license\":", stdout);
        print_json_string(metadata.license);
        fputs(",\"build_image\":", stdout);
        print_json_string(metadata.build_image);
        fputs(",\"upstream\":", stdout);
        {
            const CbsNode *upstream = NULL;
            size_t package_index;
            for (package_index = 0;
                 package_index < document->children[0]->child_count;
                 ++package_index) {
                const CbsNode *item = document->children[0]->children[package_index];
                if (item->kind == CBS_NODE_UPSTREAM) {
                    upstream = item;
                    break;
                }
            }
            if (upstream == NULL) {
                fputs("null", stdout);
            } else {
                print_upstream_json(upstream);
            }
        }
        fputs(",\"toolchain\":", stdout);
        print_json_string(metadata.toolchain);
        fputs(",\"toolchain_reason\":", stdout);
        print_json_string(metadata.toolchain_reason);
        fputs(",\"capabilities\":[", stdout);
        {
            const CbsNode *package = document->children[0];
            int first_capability = 1;
            for (index = 0; index < package->child_count; ++index) {
                const CbsNode *item = package->children[index];
                if (item->kind != CBS_NODE_CAPABILITY)
                    continue;
                if (!first_capability)
                    putchar(',');
                first_capability = 0;
                print_json_string(item->value);
            }
        }
        fputs("],\"metadata\":{", stdout);
        {
            const CbsNode *package = document->children[0];
            size_t metadata_index;
            int first_metadata = 1;
            for (metadata_index = 0; metadata_index < package->child_count;
                 ++metadata_index) {
                const CbsNode *item = package->children[metadata_index];
                size_t property_index;
                if (item->kind != CBS_NODE_METADATA)
                    continue;
                for (property_index = 0; property_index < item->child_count;
                     ++property_index) {
                    const CbsNode *property = item->children[property_index];
                    if (!first_metadata)
                        putchar(',');
                    first_metadata = 0;
                    print_json_string(property->name);
                    putchar(':');
                    print_json_string(property->value);
                }
            }
        }
        fputs("},\"tools\":[", stdout);
        {
            const CbsNode *tools = explained_tools(document);
            int first_tool = 1;
            if (tools != NULL) {
                for (child_index = 0; child_index < tools->child_count;
                     ++child_index) {
                    const CbsNode *tool = tools->children[child_index];
                    if (!first_tool)
                        putchar(',');
                    first_tool = 0;
                    fputs("{\"kind\":", stdout);
                    print_json_string(tool->name);
                    fputs(",\"policy\":", stdout);
                    print_json_string("alias");
                    fputs(",\"source\":", stdout);
                    print_json_string(tool->value);
                    putchar('}');
                }
            }
        }
        fputs("],\"command_path\":", stdout);
        print_json_string(CBS_DEFAULT_COMMAND_PATH);
        fputs(",\"library_path\":", stdout);
        print_json_string(CBS_DEFAULT_LIBRARY_PATH);
        fputs(",\"phases\":[", stdout);
        for (index = 0; index < plan.count; ++index)
            printf("%s{\"name\":\"%s\",\"operations\":%zu}",
                   index == 0 ? "" : ",", plan.phases[index]->name,
                   count_plan_operations(plan.phases[index]));
        puts("]}");
    } else {
        printf("%s: CPDL 1.0 execution plan (%zu phases)\n", path, plan.count);
        printf("metadata build_image=%s upstream=%s toolchain=%s "
               "capabilities=%zu\n",
               metadata.build_image == NULL ? "none" : metadata.build_image,
               metadata.upstream == NULL ? "none" : metadata.upstream,
               metadata.toolchain == NULL ? "none" : metadata.toolchain,
               metadata.capability_count);
        printf("command-path %s\n", CBS_DEFAULT_COMMAND_PATH);
        printf("library-path %s\n", CBS_DEFAULT_LIBRARY_PATH);
        {
            const CbsNode *tools = explained_tools(document);
            if (tools != NULL) {
                size_t tool_index;
                for (tool_index = 0; tool_index < tools->child_count;
                     ++tool_index) {
                    const CbsNode *tool = tools->children[tool_index];
                    printf("tool-policy %s alias %s\n", tool->name,
                           tool->value);
                }
            }
        }
        for (index = 0; index < plan.count; ++index)
            printf("%zu %s operations=%zu\n", index + 1,
                   plan.phases[index]->name,
                   count_plan_operations(plan.phases[index]));
    }
    cbs_node_destroy(document);
    cbs_token_list_destroy(&tokens);
    free(source);
    return 0;
}

typedef struct {
    char *resolved;
} CbsFinalizeCommand;

/* Resolve a finalizer against the approved command roots. */
static char *resolve_finalize_command(const char *command,
                                      const char *command_path) {
    const char *cursor;
    if (command == NULL || command[0] == '\0')
        return NULL;
    if (strchr(command, '/') != NULL)
        return access(command, X_OK) == 0 ? cbs_duplicate(command) : NULL;
    cursor = command_path == NULL ? CBS_DEFAULT_COMMAND_PATH : command_path;
    while (1) {
        const char *end = strchr(cursor, ':');
        size_t length = end == NULL ? strlen(cursor) : (size_t)(end - cursor);
        char *candidate = cbs_allocate(length + strlen(command) + 2);
        snprintf(candidate, length + strlen(command) + 2, "%.*s/%s",
                 (int)length, cursor, command);
        if (access(candidate, X_OK) == 0)
            return candidate;
        free(candidate);
        if (end == NULL)
            break;
        cursor = end + 1;
    }
    return NULL;
}

/* Run an explicit process-level finalizer against the staged root. */
static int run_finalize_command(const char *staged_root, void *user) {
    const CbsFinalizeCommand *policy = user;
    pid_t child;
    int status;
    if (policy == NULL || policy->resolved == NULL || staged_root == NULL)
        return 0;
    child = fork();
    if (child < 0)
        return 0;
    if (child == 0) {
        execl(policy->resolved, policy->resolved, staged_root, (char *)NULL);
        _exit(127);
    }
    if (waitpid(child, &status, 0) < 0)
        return 0;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

typedef struct {
    CbsBuildEventSink primary;
    void *primary_user;
    CbsBuildReport report;
} CbsReportEventState;

static int report_event_sink(const CbsBuildEvent *event, void *user) {
    CbsReportEventState *state = user;
    if (state == NULL || event == NULL)
        return 0;
    if (state->primary != NULL && !state->primary(event, state->primary_user))
        return 0;
    return cbs_build_report_consume(event, &state->report);
}

static int write_report_file(const char *path, const CbsBuildReport *report) {
    char temporary[4096];
    FILE *stream;
    int descriptor;
    if (path == NULL || report == NULL ||
        snprintf(temporary, sizeof(temporary), "%s.tmp-XXXXXX", path) >=
            (int)sizeof(temporary))
        return 0;
    descriptor = mkstemp(temporary);
    if (descriptor < 0)
        return 0;
    stream = fdopen(descriptor, "w");
    if (stream == NULL) {
        close(descriptor);
        unlink(temporary);
        return 0;
    }
    if (!cbs_build_report_write_json(report, stream) || fclose(stream) != 0) {
        unlink(temporary);
        return 0;
    }
    if (rename(temporary, path) != 0) {
        unlink(temporary);
        return 0;
    }
    return 1;
}

/* Build one recipe through the standalone package pipeline. */
static int build_file(const char *recipe, const char *architecture,
                      const char *staged, const char *output, const char *cache,
                      const char *ca_file, const char *events,
                      const char *finalize_command,
                      const char *prune_policy_path,
                      const char *firmware_root,
                      const char *command_path,
                      const char *library_path,
                      const char *report_path,
                      const CbsInputBinding *inputs, size_t input_count,
                      const CbsToolIdentity *tool_identities,
                      size_t tool_identity_count) {
    struct stat status;
    CbsFetchService service;
    CbsBuildEventSink event_sink = NULL;
    FILE *event_stream = stderr;
    int event_fd = -1;
    int result;
    char fetch_error[256];
    CbsPrunePolicy prune_policy;
    char prune_error[256];
    struct stat firmware_status;
    struct stat input_status;
    CbsFinalizeCommand finalize_policy = {0};
    CbsReportEventState report_state;
    CbsBuildEventSink effective_event_sink = NULL;
    void *effective_event_user = NULL;
    int report_written = 1;
    memset(&service, 0, sizeof(service));
    memset(&report_state, 0, sizeof(report_state));
    if (report_path != NULL)
        cbs_build_report_init(&report_state.report);
    if (command_path != NULL && !cbs_command_path_is_valid(command_path)) {
        cli_errorf("build", "CBS-E1004", "cli", 2,
                   "--command-path must contain only non-empty absolute "
                   "directories without . or .. components");
        return 2;
    }
    if (library_path != NULL && !cbs_library_path_is_valid(library_path)) {
        cli_errorf("build", "CBS-E1005", "cli", 2,
                   "--library-path must contain only non-empty absolute "
                   "directories without . or .. components");
        return 2;
    }
    if (finalize_command != NULL) {
        finalize_policy.resolved = resolve_finalize_command(
            finalize_command,
            command_path == NULL ? CBS_DEFAULT_COMMAND_PATH : command_path);
        if (finalize_policy.resolved == NULL) {
            cli_errorf("build", "CBS-E1006", "policy", 2,
                       "finalize command is not executable under the approved "
                       "command-path policy");
            return 2;
        }
    }
    if (prune_policy_path != NULL &&
        !cbs_prune_policy_load(prune_policy_path, &prune_policy,
                               prune_error, sizeof(prune_error))) {
        cli_errorf("build", "CBS-E1007", "policy", 2, "%s", prune_error);
        return 2;
    }
    if (firmware_root != NULL &&
        (stat(firmware_root, &firmware_status) != 0 ||
         !S_ISDIR(firmware_status.st_mode))) {
        cli_errorf(firmware_root, "CBS-E1008", "input", 3,
                   "firmware root is not an accessible directory");
        return 3;
    }
    for (size_t input_index = 0; input_index < input_count; ++input_index) {
        if (stat(inputs[input_index].path, &input_status) != 0 ||
            !S_ISREG(input_status.st_mode)) {
            cli_errorf(inputs[input_index].path, "CBS-E1009", "input", 3,
                       "input `%s` is not an accessible regular file",
                       inputs[input_index].name);
            return 3;
        }
    }
    if (events != NULL) {
        if (strcmp(events, "human") == 0)
            event_sink = cbs_build_event_human;
        else if (strcmp(events, "jsonl") == 0)
            event_sink = cbs_build_event_jsonl;
        else {
            cli_errorf("build", "CBS-E1010", "cli", 2,
                       "--events must be human or jsonl");
            return 2;
        }
        event_fd = dup(fileno(stderr));
        if (event_fd < 0 || (event_stream = fdopen(event_fd, "w")) == NULL) {
            if (event_fd >= 0)
                close(event_fd);
            cli_errorf("build", "CBS-E1011", "internal", 3,
                       "cannot initialize event reporter");
            return 3;
        }
    }
    effective_event_sink = event_sink;
    effective_event_user = event_stream;
    if (report_path != NULL) {
        report_state.primary = event_sink;
        report_state.primary_user = event_stream;
        effective_event_sink = report_event_sink;
        effective_event_user = &report_state;
    }
    if (cache != NULL &&
        (stat(cache, &status) != 0 || !S_ISDIR(status.st_mode))) {
        cli_errorf(cache, "CBS-E1012", "input", 3,
                   "cache directory is not accessible");
        return 3;
    }
    if (ca_file != NULL &&
        (stat(ca_file, &status) != 0 || !S_ISREG(status.st_mode))) {
        cli_errorf(ca_file, "CBS-E1013", "input", 3,
                   "CA file is not accessible");
        return 3;
    }
    /* Cache hits must work in a network-less image without libcurl. */
    (void)cbs_cli_fetch_service_with_ca(&service, fetch_error,
                                        sizeof(fetch_error), ca_file);
    result = cbs_build_standalone_with_events_policy_path_inputs_tool_identities_result(
            recipe, staged, output, architecture, &service, cache,
            finalize_command == NULL ? NULL : run_finalize_command,
            finalize_command == NULL ? NULL : (void *)&finalize_policy,
            firmware_root,
            prune_policy_path == NULL ? NULL : &prune_policy, command_path,
            library_path, inputs, input_count, tool_identities,
            tool_identity_count, effective_event_sink, effective_event_user,
            report_path == NULL ? NULL : report_state.report.fingerprint);
    if (event_stream != stderr)
        fclose(event_stream);
    if (report_path != NULL) {
        strncpy(report_state.report.recipe_path, recipe,
                sizeof(report_state.report.recipe_path) - 1);
        report_state.report.recipe_path[
            sizeof(report_state.report.recipe_path) - 1] = '\0';
        report_state.report.status = result ? 0 : 1;
    if (report_path != NULL && report_state.report.fingerprint[0] == '\0')
        snprintf(report_state.report.fingerprint,
                 sizeof(report_state.report.fingerprint), "unavailable");
        if (output != NULL) {
            strncpy(report_state.report.artifact_path, output,
                    sizeof(report_state.report.artifact_path) - 1);
            report_state.report.artifact_path[
                sizeof(report_state.report.artifact_path) - 1] = '\0';
            cbs_digest_file(output, report_state.report.artifact_digest);
        }
        report_written = write_report_file(report_path, &report_state.report);
        if (!report_written) {
            cli_errorf(report_path, "CBS-E1020", "report", 3,
                       "cannot write report atomically");
            return 3;
        }
    }
    free(finalize_policy.resolved);
    if (!result) {
        cli_errorf("build", "CBS-E1014", "runtime", 3,
                   "recipe, staged tree, or package output was rejected");
        return 3;
    }
    if (output == NULL)
        printf("staged %s\n", staged);
    else
        printf("built %s\n", output);
    return 0;
}

typedef struct {
    const char *root;
    const char *name;
    const char *version;
    const char *architecture;
    const char *output;
    const char *license;
    long release;
    int release_set;
} CbsPackageOptions;

static int parse_package_options(int argc, char **argv,
                                 CbsPackageOptions *options) {
    int index;
    memset(options, 0, sizeof(*options));
    if (argc < 3 || strcmp(argv[1], "package") != 0) {
        cli_errorf("package", "CBS-E1015", "cli", 2, "missing staged tree");
        return 0;
    }
    options->root = argv[2];
    for (index = 3; index < argc; ++index) {
        const char *argument = argv[index];
        const char *value = NULL;
        if (is_diagnostic_option(argument))
            continue;
        if (strncmp(argument, "--name=", 7) == 0)
            value = argument + 7;
        else if (strncmp(argument, "--version=", 10) == 0)
            value = argument + 10;
        else if (strncmp(argument, "--release=", 10) == 0)
            value = argument + 10;
        else if (strncmp(argument, "--arch=", 7) == 0)
            value = argument + 7;
        else if (strncmp(argument, "--output=", 9) == 0)
            value = argument + 9;
        else if (strncmp(argument, "--license=", 10) == 0)
            value = argument + 10;
        else if (strcmp(argument, "--name") == 0 ||
                 strcmp(argument, "--version") == 0 ||
                 strcmp(argument, "--release") == 0 ||
                 strcmp(argument, "--arch") == 0 ||
                 strcmp(argument, "--output") == 0 ||
                 strcmp(argument, "--license") == 0) {
            if (++index >= argc) {
                cli_errorf("package", "CBS-E1001", "cli", 2,
                           "option `%s` requires a value", argument);
                return 0;
            }
            value = argv[index];
        } else {
            cli_errorf("package", "CBS-E1001", "cli", 2,
                       "unknown option `%s`", argument);
            return 0;
        }
        if (value == NULL || value[0] == '\0') {
            cli_errorf("package", "CBS-E1001", "cli", 2,
                       "option `%s` requires a non-empty value", argument);
            return 0;
        }
        if (strcmp(argument, "--name") == 0 ||
            strncmp(argument, "--name=", 7) == 0)
            options->name = value;
        else if (strcmp(argument, "--version") == 0 ||
                 strncmp(argument, "--version=", 10) == 0)
            options->version = value;
        else if (strcmp(argument, "--release") == 0 ||
                 strncmp(argument, "--release=", 10) == 0) {
            char *end;
            options->release = strtol(value, &end, 10);
            if (*end != '\0' || options->release <= 0) {
                cli_errorf("package", "CBS-E1016", "validation", 2,
                           "--release must be positive");
                return 0;
            }
            options->release_set = 1;
        } else if (strcmp(argument, "--arch") == 0 ||
                   strncmp(argument, "--arch=", 7) == 0)
            options->architecture = value;
        else if (strcmp(argument, "--output") == 0 ||
                 strncmp(argument, "--output=", 9) == 0)
            options->output = value;
        else
            options->license = value;
    }
    if (options->name == NULL || options->version == NULL ||
        !options->release_set || options->architecture == NULL ||
        options->output == NULL) {
        cli_errorf("package", "CBS-E1017", "cli", 2,
                   "--name, --version, --release, --arch, and --output are required");
        return 0;
    }
    return 1;
}

static int package_file(const CbsPackageOptions *options) {
    CbsPackageIdentity identity;
    struct stat status;
    if (stat(options->root, &status) != 0 || !S_ISDIR(status.st_mode)) {
        cli_errorf(options->root, "CBS-E1018", "input", 3,
                   "staged tree is not an accessible directory");
        return 3;
    }
    memset(&identity, 0, sizeof(identity));
    identity.name = options->name;
    identity.version = options->version;
    identity.release = options->release;
    identity.architecture = options->architecture;
    if (!cbs_package_staged_tree(options->root, &identity, options->license,
                                 options->output)) {
        cli_errorf("package", "CBS-E1019", "runtime", 3,
                   "staged tree or package output was rejected");
        return 3;
    }
    printf("packaged %s\n", options->output);
    return 0;
}

typedef struct {
    const char *recipe;
    const char *architecture;
    const char *staged;
    const char *cache;
    const char *command_path;
    const char *library_path;
    const char *report;
    const char *events;
} CbsDoctorOptions;

static void doctor_result(const char *check, int pass, const char *message,
                          const char *subject) {
    if (cbs_diagnostic_is_json()) {
        cbs_cli_diagnostic(pass ? "info" : "error",
                           pass ? "CBS-D0000" : "CBS-D0001", "doctor",
                           message, subject == NULL ? check : subject,
                           pass ? 0 : 3);
    } else {
        printf("doctor: %s %s: %s\n", pass ? "PASS" : "FAIL", check,
               message);
    }
}

static int doctor_directory(const char *check, const char *path) {
    struct stat status;
    int pass = path != NULL && stat(path, &status) == 0 &&
               S_ISDIR(status.st_mode) && access(path, W_OK) == 0;
    doctor_result(check, pass,
                  pass ? "directory is accessible and writable"
                       : "directory is missing, not writable, or not a directory",
                  path);
    return pass;
}

static int doctor_destination(const char *check, const char *path) {
    struct stat status;
    char parent[4096];
    char *slash;
    int pass;
    if (path == NULL)
        return 1;
    if (stat(path, &status) == 0)
        pass = S_ISREG(status.st_mode) && access(path, W_OK) == 0;
    else {
        if (snprintf(parent, sizeof(parent), "%s", path) >=
            (int)sizeof(parent))
            pass = 0;
        else {
            slash = strrchr(parent, '/');
            if (slash == NULL)
                pass = access(".", W_OK) == 0;
            else {
                if (slash == parent)
                    slash[1] = '\0';
                else
                    *slash = '\0';
                pass = access(parent, W_OK) == 0;
            }
        }
    }
    doctor_result(check, pass,
                  pass ? "destination is writable or can be created"
                       : "destination is not writable and its parent cannot create it",
                  path);
    return pass;
}

static int doctor_valid_architecture(const char *architecture) {
    size_t index;
    if (architecture == NULL || architecture[0] == '\0')
        return 0;
    for (index = 0; architecture[index] != '\0'; ++index)
        if (!(isalnum((unsigned char)architecture[index]) ||
              architecture[index] == '.' || architecture[index] == '_' ||
              architecture[index] == '-'))
            return 0;
    return 1;
}

static int doctor_locale(void) {
    const char *locale = cbs_select_utf8_locale();
    int pass = locale != NULL;
    doctor_result("locale", pass,
                  pass ? "selected UTF-8 LC_CTYPE" :
                         "no usable UTF-8 LC_CTYPE is available",
                  locale);
    return pass;
}

static int doctor_temporary_space(const char *path) {
    struct statvfs status;
    unsigned long long available;
    int pass = path != NULL && statvfs(path, &status) == 0;
    available = pass ? (unsigned long long)status.f_bavail * status.f_frsize : 0;
    pass = pass && available >= 64ULL * 1024ULL * 1024ULL;
    doctor_result("temporary space", pass,
                  pass ? "at least 64 MiB is available" :
                         "temporary filesystem is unavailable or below 64 MiB",
                  path);
    return pass;
}

static void doctor_walk(const CbsNode *node, const char *command_path,
                        const char *library_path, size_t *failures) {
    size_t index;
    if (node == NULL)
        return;
    if ((node->kind == CBS_NODE_RUN || node->kind == CBS_NODE_TOOL) &&
        node->value != NULL) {
        char *found = NULL;
        int pass = 0;
        if (strchr(node->value, '$') != NULL) {
            doctor_result("command", 1,
                          "command is resolved during build because it uses an interpolation",
                          node->value);
            pass = 1;
        } else if (node->value[0] != '/' && strchr(node->value, '/') != NULL) {
            doctor_result("command", 1,
                          "relative command is resolved after the build directory is materialized",
                          node->value);
            pass = 1;
        } else {
            found = cbs_resolve_executable(node->value, "/", command_path);
            pass = found != NULL && access(found, X_OK) == 0;
        }
        doctor_result("command", pass,
                      pass ? "executable resolves under the command-path policy"
                           : "executable is absent under the command-path policy",
                      node->value);
        if (!pass)
            ++*failures;
        free(found);
    } else if (node->kind == CBS_NODE_STAGE && node->value != NULL) {
        char searched[2048];
        char *found;
        int pass = node->value[0] == '/';
        int want_tree = strcmp(node->name == NULL ? "" : node->name, "tree") == 0;
        found = pass ? cbs_resolve_stage_source(node->value, command_path,
                                                library_path, want_tree,
                                                searched, sizeof(searched)) : NULL;
        pass = pass && found != NULL;
        doctor_result("stage source", pass,
                      pass ? "source exists under an approved image path"
                           : "source is absent from approved image paths",
                      node->value);
        if (!pass)
            ++*failures;
        free(found);
    }
    for (index = 0; index < node->child_count; ++index)
        doctor_walk(node->children[index], command_path, library_path,
                    failures);
}

static int parse_doctor_options(int argc, char **argv,
                                CbsDoctorOptions *options) {
    int index;
    memset(options, 0, sizeof(*options));
    for (index = 2; index < argc; ++index) {
        const char *argument = argv[index];
        const char *value = NULL;
        if (is_diagnostic_option(argument))
            continue;
        if (strcmp(argument, "--json") == 0) {
            cbs_diagnostic_set_json(1);
            continue;
        }
        if (argument[0] != '-' && options->recipe == NULL) {
            options->recipe = argument;
            continue;
        }
        if (strncmp(argument, "--arch=", 7) == 0)
            value = argument + 7;
        else if (strncmp(argument, "--staged=", 9) == 0)
            value = argument + 9;
        else if (strncmp(argument, "--cache=", 8) == 0)
            value = argument + 8;
        else if (strncmp(argument, "--command-path=", 15) == 0)
            value = argument + 15;
        else if (strncmp(argument, "--library-path=", 15) == 0)
            value = argument + 15;
        else if (strncmp(argument, "--report=", 9) == 0)
            value = argument + 9;
        else if (strncmp(argument, "--events=", 9) == 0)
            value = argument + 9;
        else if (strcmp(argument, "--arch") == 0 ||
                 strcmp(argument, "--staged") == 0 ||
                 strcmp(argument, "--cache") == 0 ||
                 strcmp(argument, "--command-path") == 0 ||
                 strcmp(argument, "--library-path") == 0 ||
                 strcmp(argument, "--report") == 0 ||
                 strcmp(argument, "--events") == 0) {
            if (++index >= argc) {
                cli_errorf("doctor", "CBS-E1001", "cli", 2,
                           "option `%s` requires a value", argument);
                return 0;
            }
            value = argv[index];
        } else {
            cli_errorf("doctor", "CBS-E1001", "cli", 2,
                       "unknown option `%s`", argument);
            return 0;
        }
        if (value == NULL || value[0] == '\0') {
            cli_errorf("doctor", "CBS-E1001", "cli", 2,
                       "option `%s` requires a non-empty value", argument);
            return 0;
        }
        if (strcmp(argument, "--arch") == 0 ||
            strncmp(argument, "--arch=", 7) == 0)
            options->architecture = value;
        else if (strcmp(argument, "--staged") == 0 ||
                 strncmp(argument, "--staged=", 9) == 0)
            options->staged = value;
        else if (strcmp(argument, "--cache") == 0 ||
                 strncmp(argument, "--cache=", 8) == 0)
            options->cache = value;
        else if (strcmp(argument, "--command-path") == 0 ||
                 strncmp(argument, "--command-path=", 15) == 0)
            options->command_path = value;
        else if (strcmp(argument, "--report") == 0 ||
                 strncmp(argument, "--report=", 9) == 0)
            options->report = value;
        else if (strcmp(argument, "--events") == 0 ||
                 strncmp(argument, "--events=", 9) == 0)
            options->events = value;
        else
            options->library_path = value;
    }
    return 1;
}

static int doctor_file(const CbsDoctorOptions *options) {
    const char *command_path = options->command_path == NULL
                                   ? CBS_DEFAULT_COMMAND_PATH
                                   : options->command_path;
    const char *library_path = options->library_path == NULL
                                   ? CBS_DEFAULT_LIBRARY_PATH
                                   : options->library_path;
    CbsTokenList tokens = {0};
    CbsNode *document = NULL;
    char *source = NULL;
    size_t length = 0;
    size_t failures = 0;
    int pass = 1;

    if (!cbs_command_path_is_valid(command_path)) {
        doctor_result("command path", 0, "invalid absolute directory list",
                      command_path);
        ++failures;
    } else
        doctor_result("command path", 1, "policy is valid", command_path);
    if (!cbs_library_path_is_valid(library_path)) {
        doctor_result("library path", 0, "invalid absolute directory list",
                      library_path);
        ++failures;
    } else
        doctor_result("library path", 1, "policy is valid", library_path);
    if (options->staged != NULL &&
        !doctor_directory("staged workspace", options->staged))
        ++failures;
    if (options->cache != NULL &&
        !doctor_directory("source cache", options->cache))
        ++failures;
    if (!doctor_locale())
        ++failures;
    if (!doctor_temporary_space(options->staged != NULL ? options->staged :
                                 options->cache != NULL ? options->cache : "/tmp"))
        ++failures;
    if (options->architecture != NULL)
        if (!doctor_valid_architecture(options->architecture))
            ++failures;
    if (options->architecture != NULL)
        doctor_result("architecture", doctor_valid_architecture(options->architecture),
                      "architecture uses only portable identity characters",
                      options->architecture);
    if (options->report != NULL && !doctor_destination("report", options->report))
        ++failures;
    if (options->events != NULL && !doctor_destination("events", options->events))
        ++failures;
    if (options->recipe == NULL) {
        doctor_result("runtime", 1, "no recipe supplied; static checks complete",
                      NULL);
        return failures == 0 ? 0 : 3;
    }
    if (!has_cbs_extension(options->recipe)) {
        cli_errorf(options->recipe, "CPDL-E3004", "validation", 3,
                   "recipe must use the .cbs extension");
        return 3;
    }
    source = read_file(options->recipe, &length);
    if (source == NULL)
        return 3;
    if (!cbs_lex(options->recipe, source, length, &tokens))
        goto cleanup;
    document = cbs_parse(options->recipe, source, length, &tokens);
    if (document == NULL || !cbs_validate(document, options->recipe, source))
        goto cleanup;
    doctor_result("recipe", 1, "valid CPDL and execution plan", options->recipe);
    doctor_walk(document, command_path, library_path, &failures);
cleanup:
    pass = failures == 0 && document != NULL;
    cbs_node_destroy(document);
    cbs_token_list_destroy(&tokens);
    free(source);
    doctor_result("summary", pass, pass ? "all requested preflight checks pass"
                                         : "one or more preflight checks failed",
                  options->recipe);
    return pass ? 0 : 3;
}

typedef struct {
    const char *recipe;
    const char *architecture;
    const char *command_path;
    const char *library_path;
    CbsInputBinding inputs[32];
    size_t input_count;
    CbsToolIdentity tool_identities[32];
    size_t tool_identity_count;
} CbsFingerprintOptions;

static int add_fingerprint_input(CbsFingerprintOptions *options,
                                 const char *spec) {
    const char *separator = strchr(spec, '=');
    char *name;
    if (options->input_count == 32 || separator == NULL || separator == spec ||
        separator[1] != '/') {
        cli_errorf("fingerprint", "CBS-E1001", "cli", 2,
                   "--input requires NAME=ABSOLUTE_FILE with a portable name");
        return 0;
    }
    name = cbs_duplicate_range(spec, (size_t)(separator - spec));
    if (!valid_input_name(name)) {
        free(name);
        cli_errorf("fingerprint", "CBS-E1001", "cli", 2,
                   "--input requires NAME=ABSOLUTE_FILE with a portable name");
        return 0;
    }
    for (size_t index = 0; index < options->input_count; ++index)
        if (strcmp(options->inputs[index].name, name) == 0) {
            cli_errorf("fingerprint", "CBS-E1002", "cli", 2,
                       "duplicate --input name `%s`", name);
            free(name);
            return 0;
        }
    options->inputs[options->input_count].name = name;
    options->inputs[options->input_count].path = cbs_duplicate(separator + 1);
    ++options->input_count;
    return 1;
}

static void free_fingerprint_inputs(CbsFingerprintOptions *options) {
    for (size_t index = 0; index < options->input_count; ++index) {
        free((void *)options->inputs[index].name);
        free((void *)options->inputs[index].path);
    }
}

static int parse_fingerprint_options(int argc, char **argv,
                                     CbsFingerprintOptions *options) {
    int index;
    memset(options, 0, sizeof(*options));
    if (argc < 3 || strcmp(argv[1], "fingerprint") != 0) {
        cli_errorf("fingerprint", "CBS-E1001", "cli", 2,
                   "missing recipe");
        return 0;
    }
    options->recipe = argv[2];
    for (index = 3; index < argc; ++index) {
        const char *argument = argv[index];
        const char *value = NULL;
        if (is_diagnostic_option(argument))
            continue;
        if (strncmp(argument, "--input=", 8) == 0) {
            if (!add_fingerprint_input(options, argument + 8))
                return 0;
            continue;
        }
        if (strcmp(argument, "--input") == 0) {
            if (++index >= argc || !add_fingerprint_input(options, argv[index]))
                return 0;
            continue;
        }
        if (strncmp(argument, "--tool-identity=", 16) == 0) {
            if (!add_tool_identity(options->tool_identities,
                                   &options->tool_identity_count,
                                   argument + 16, "fingerprint"))
                return 0;
            continue;
        }
        if (strcmp(argument, "--tool-identity") == 0) {
            if (++index >= argc ||
                !add_tool_identity(options->tool_identities,
                                   &options->tool_identity_count, argv[index],
                                   "fingerprint")) {
                if (index >= argc)
                    cli_errorf("fingerprint", "CBS-E1001", "cli", 2,
                               "option `--tool-identity` requires a value");
                return 0;
            }
            continue;
        }
        if (strncmp(argument, "--arch=", 7) == 0)
            value = argument + 7;
        else if (strncmp(argument, "--command-path=", 15) == 0)
            value = argument + 15;
        else if (strncmp(argument, "--library-path=", 15) == 0)
            value = argument + 15;
        else if (strcmp(argument, "--arch") == 0 ||
                 strcmp(argument, "--command-path") == 0 ||
                 strcmp(argument, "--library-path") == 0) {
            if (++index >= argc) {
                cli_errorf("fingerprint", "CBS-E1001", "cli", 2,
                           "option `%s` requires a value", argument);
                return 0;
            }
            value = argv[index];
        } else {
            cli_errorf("fingerprint", "CBS-E1001", "cli", 2,
                       "unknown option `%s`", argument);
            return 0;
        }
        if (strcmp(argument, "--arch") == 0 ||
            strncmp(argument, "--arch=", 7) == 0)
            options->architecture = value;
        else if (strcmp(argument, "--command-path") == 0 ||
                 strncmp(argument, "--command-path=", 15) == 0)
            options->command_path = value;
        else
            options->library_path = value;
    }
    if (options->architecture == NULL || options->architecture[0] == '\0') {
        cli_errorf("fingerprint", "CBS-E1001", "cli", 2,
                   "--arch is required");
        return 0;
    }
    return 1;
}

static int fingerprint_file(CbsFingerprintOptions *options) {
    char fingerprint[65];
    CbsFingerprintContext context = {0};
    context.tool_identities = options->tool_identities;
    context.tool_identity_count = options->tool_identity_count;
    if ((options->tool_identity_count == 0 && !cbs_build_fingerprint(
             options->recipe, options->architecture, options->command_path,
             options->library_path, options->inputs, options->input_count,
             fingerprint)) ||
        (options->tool_identity_count != 0 &&
         !cbs_build_fingerprint_with_context(
             options->recipe, options->architecture, options->command_path,
             options->library_path, options->inputs, options->input_count,
             &context, fingerprint))) {
        cli_errorf(options->recipe, "CBS-E1021", "fingerprint", 3,
                   "cannot validate recipe or measure all build inputs");
        free_fingerprint_inputs(options);
        free_tool_identities(options->tool_identities,
                             options->tool_identity_count);
        return 3;
    }
    printf("fingerprint %s\n", fingerprint);
    free_fingerprint_inputs((CbsFingerprintOptions *)options);
    free_tool_identities(options->tool_identities,
                         options->tool_identity_count);
    return 0;
}

static int list_file(const char *path, int json, const char *prefix, char type) {
    if (!cbs_cixpkg_list_filtered(path, stdout, json, prefix, type)) {
        cli_errorf(path, "CIXPKG-E4001", "artifact", 4,
                   "artifact verification or manifest listing failed");
        return 4;
    }
    return 0;
}

static int diff_file(const char *left, const char *right, int json,
                     const char *prefix, char type) {
    int different = 0;
    if (!cbs_cixpkg_diff_filtered(left, right, stdout, json, &different,
                                  prefix, type)) {
        cli_errorf(left, "CIXPKG-E4001", "artifact", 4,
                   "artifact verification or manifest diff failed");
        return 4;
    }
    return different ? 1 : 0;
}

static int parse_entry_filters(int argc, char **argv, int start,
                               const char *verb, const char **prefix,
                               char *type) {
    int index;
    *prefix = NULL;
    *type = '\0';
    for (index = start; index < argc; ++index) {
        const char *argument = argv[index];
        const char *value = NULL;
        if (strcmp(argument, "--json") == 0 || is_diagnostic_option(argument))
            continue;
        if (strncmp(argument, "--path-prefix=", 14) == 0)
            value = argument + 14;
        else if (strncmp(argument, "--type=", 7) == 0)
            value = argument + 7;
        else if (strcmp(argument, "--path-prefix") == 0 ||
                 strcmp(argument, "--type") == 0) {
            if (++index >= argc) {
                cli_errorf(verb, "CBS-E1001", "cli", 2,
                           "option `%s` requires a value", argument);
                return 0;
            }
            value = argv[index];
        } else {
            cli_errorf(verb, "CBS-E1001", "cli", 2,
                       "unknown option `%s`", argument);
            return 0;
        }
        if (value == NULL || value[0] == '\0') {
            cli_errorf(verb, "CBS-E1001", "cli", 2,
                       "filter value must not be empty");
            return 0;
        }
        if (strncmp(argument, "--path-prefix", 13) == 0 ||
            strcmp(argument, "--path-prefix") == 0)
            *prefix = value;
        else {
            if (strcmp(value, "f") == 0 || strcmp(value, "file") == 0)
                *type = 'f';
            else if (strcmp(value, "d") == 0 || strcmp(value, "directory") == 0)
                *type = 'd';
            else if (strcmp(value, "l") == 0 || strcmp(value, "symlink") == 0)
                *type = 'l';
            else {
                cli_errorf(verb, "CBS-E1001", "cli", 2,
                           "--type must be file, directory, or symlink");
                return 0;
            }
        }
    }
    return 1;
}

static void print_capabilities(void) {
    fputs("{\"schema\":\"cbs.capabilities/v1\",\"version\":", stdout);
    print_json_string(cbs_version());
    printf(",\"api_version\":%u,\"abi_version\":%u,\"cpdl_version\":%u,\"cpdl_contract\":\"%s\",\"cixpkg_version\":%u,\"execution_context_size\":%zu,\"integration\":[\"child-process\",\"static-library\"],\"commands\":[\"build\",\"doctor\",\"explain\",\"fingerprint\",\"list\",\"diff\",\"package\",\"revise\",\"verify\"]}\n",
           cbs_api_version(), cbs_abi_version(), CBS_CPDL_VERSION,
           CBS_CPDL_CONTRACT, CBS_CIXPKG_VERSION, cbs_execution_context_size());
}

/* Print recipe identity, source, and optional artifact digest metadata. */
static int inspect_file(const char *path, const char *artifact) {
    char *source;
    size_t length;
    char recipe_digest[65], artifact_digest[65];
    CbsTokenList tokens = {0};
    CbsNode *document;
    CbsSourceSet sources = {0};
    CbsBuildMetadata metadata;
    size_t i;
    source = read_file(path, &length);
    if (source == NULL)
        return 3;
    if (!cbs_lex(path, source, length, &tokens))
        return 3;
    document = cbs_parse(path, source, length, &tokens);
    if (document == NULL || !cbs_validate(document, path, source) ||
        !cbs_sources_from_document(document, &sources) ||
        !cbs_digest_text(source, length, recipe_digest) ||
        !cbs_build_metadata(document, &metadata))
        return 3;
    printf("recipe-digest %s\n", recipe_digest);
    if (metadata.license != NULL)
        printf("license %s\n", metadata.license);
    for (i = 0; i < sources.count; ++i)
        printf("source-digest %s %s\n", sources.items[i].name,
               sources.items[i].sha256);
    if (artifact != NULL && cbs_digest_file(artifact, artifact_digest))
        printf("artifact-digest %s\n", artifact_digest);
    if (artifact != NULL) {
        char license[4096];
        if (!cbs_cixpkg_read_license(artifact, license, sizeof(license)))
            return 3;
        if (license[0] != '\0')
            printf("artifact-license %s\n", license);
    }
    cbs_source_set_destroy(&sources);
    cbs_node_destroy(document);
    cbs_token_list_destroy(&tokens);
    free(source);
    return 0;
}

/* Dispatch the command-line request selected by the user. */
int main(int argc, char **argv) {
    int argument_index;
    for (argument_index = 1; argument_index < argc; ++argument_index)
        if (is_diagnostic_option(argv[argument_index]))
            cbs_diagnostic_set_json(1);
    if (argc > 1)
        cbs_diagnostic_set_verb(argv[1]);
    if (argc == 2 &&
        (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        usage(stdout);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        printf("cbs %s\n", CBS_VERSION);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--capabilities") == 0) {
        print_capabilities();
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "doctor") == 0) {
        CbsDoctorOptions options;
        if (!parse_doctor_options(argc, argv, &options))
            return 2;
        return doctor_file(&options);
    }
    if (argc >= 2 && strcmp(argv[1], "fingerprint") == 0) {
        CbsFingerprintOptions options;
        if (!parse_fingerprint_options(argc, argv, &options))
            return 2;
        return fingerprint_file(&options);
    }
    if (argc >= 3 && strcmp(argv[1], "list") == 0) {
        int json = 0;
        const char *prefix;
        char type;
        for (argument_index = 3; argument_index < argc; ++argument_index)
            if (strcmp(argv[argument_index], "--json") == 0)
                json = 1;
        if (!parse_entry_filters(argc, argv, 3, "list", &prefix, &type))
            return 2;
        return list_file(argv[2], json, prefix, type);
    }
    if (argc >= 4 && strcmp(argv[1], "diff") == 0) {
        int json = 0;
        const char *prefix;
        char type;
        for (argument_index = 4; argument_index < argc; ++argument_index)
            if (strcmp(argv[argument_index], "--json") == 0)
                json = 1;
        if (!parse_entry_filters(argc, argv, 4, "diff", &prefix, &type))
            return 2;
        return diff_file(argv[2], argv[3], json, prefix, type);
    }
    if (argc >= 3 && strcmp(argv[1], "verify") == 0) {
        for (argument_index = 3; argument_index < argc; ++argument_index)
            if (!is_diagnostic_option(argv[argument_index])) {
                cli_errorf("verify", "CBS-E1001", "cli", 2,
                           "unknown option `%s`", argv[argument_index]);
                return 2;
            }
        return verify_file(argv[2]);
    }
    if (argc >= 3 && strcmp(argv[1], "explain") == 0) {
        int json = 0;
        for (argument_index = 3; argument_index < argc; ++argument_index)
            if (strcmp(argv[argument_index], "--json") == 0)
                json = 1;
            else if (!is_diagnostic_option(argv[argument_index])) {
                cli_errorf("explain", "CBS-E1001", "cli", 2,
                           "unknown option `%s`", argv[argument_index]);
                return 2;
            }
        return explain_file(argv[2], json);
    }
    if (argc >= 3 && strcmp(argv[1], "revise") == 0) {
        ReviseRequest requests[32] = {0};
        size_t request_count = 0;
        const char *output = NULL;
        int valid = 1;
        for (argument_index = 3; argument_index < argc; ++argument_index) {
            const char *argument = argv[argument_index];
            const char *spec = NULL;
            int unset = 0;
            if (is_diagnostic_option(argument))
                continue;
            if (strncmp(argument, "--set=", 6) == 0) {
                spec = argument + 6;
            } else if (strcmp(argument, "--set") == 0) {
                if (++argument_index >= argc) {
                    cli_errorf("revise", "CBS-E1001", "cli", 2,
                               "option `--set` requires a value");
                    valid = 0;
                    break;
                }
                spec = argv[argument_index];
            } else if (strncmp(argument, "--unset=", 8) == 0) {
                spec = argument + 8;
                unset = 1;
            } else if (strcmp(argument, "--unset") == 0) {
                if (++argument_index >= argc) {
                    cli_errorf("revise", "CBS-E1001", "cli", 2,
                               "option `--unset` requires a target");
                    valid = 0;
                    break;
                }
                spec = argv[argument_index];
                unset = 1;
            } else if (strncmp(argument, "--output=", 9) == 0) {
                output = argument + 9;
                continue;
            } else if (strcmp(argument, "--output") == 0) {
                if (++argument_index >= argc || argv[argument_index][0] == '\0') {
                    cli_errorf("revise", "CBS-E1001", "cli", 2,
                               "option `--output` requires a value");
                    valid = 0;
                    break;
                }
                output = argv[argument_index];
                continue;
            } else {
                cli_errorf("revise", "CBS-E1001", "cli", 2,
                           "unknown option `%s`", argument);
                valid = 0;
                break;
            }
            if (request_count == 32 ||
                !parse_revise_request(spec, unset, &requests[request_count])) {
                cli_errorf("revise", "CBS-E1001", "cli", 2,
                           "invalid revision target `%s`", spec);
                valid = 0;
                break;
            }
            ++request_count;
        }
        if (!valid) {
            free_revise_requests(requests, request_count);
            return 2;
        }
        valid = revise_file(argv[2], requests, request_count, output);
        free_revise_requests(requests, request_count);
        return valid;
    }
    if (argc >= 3 &&
        (strcmp(argv[1], "check") == 0 || strcmp(argv[1], "validate") == 0)) {
        for (argument_index = 3; argument_index < argc; ++argument_index)
            if (strcmp(argv[argument_index], "--json") == 0)
                cbs_diagnostic_set_json(1);
            else if (!is_diagnostic_option(argv[argument_index])) {
                cli_errorf(argv[1], "CBS-E1001", "cli", 2,
                           "unknown option `%s`", argv[argument_index]);
                return 2;
            }
        return validate_file(argv[2]);
    }
    if (argc >= 5 && strcmp(argv[1], "extract") == 0) {
        int found_into = 0;
        for (argument_index = 3; argument_index + 1 < argc;
             ++argument_index)
            if (strcmp(argv[argument_index], "--into") == 0) {
                if (found_into || argument_index + 2 != argc &&
                    !is_diagnostic_option(argv[argument_index + 2])) {
                    cli_errorf("extract", "CBS-E1001", "cli", 2,
                               "unknown or duplicate extract option");
                    return 2;
                }
                found_into = 1;
                return extract_file(argv[2], argv[argument_index + 1]);
            } else if (!is_diagnostic_option(argv[argument_index])) {
                cli_errorf("extract", "CBS-E1001", "cli", 2,
                           "unknown option `%s`", argv[argument_index]);
                return 2;
            }
        cli_errorf("extract", "CBS-E1001", "cli", 2,
                   "--into requires a destination");
        return 2;
    }
    if (argc >= 3 && strcmp(argv[1], "build") == 0) {
        CbsBuildOptions options;
        int result;
        if (!parse_build_options(argc, argv, &options))
            return 2;
        result = build_file(options.recipe, options.architecture, options.staged,
                          options.output, options.cache, options.ca_file,
                      options.events, options.finalize_command,
                          options.prune_policy, options.firmware_root,
                          options.command_path, options.library_path,
                          options.report_path,
                          options.inputs, options.input_count,
                          options.tool_identities, options.tool_identity_count);
        free_inputs(&options);
        free_tool_identities(options.tool_identities,
                             options.tool_identity_count);
        return result;
    }
    if (argc >= 3 && strcmp(argv[1], "package") == 0) {
        CbsPackageOptions options;
        if (!parse_package_options(argc, argv, &options))
            return 2;
        return package_file(&options);
    }
    if (argc >= 3 && strcmp(argv[1], "inspect") == 0) {
        const char *paths[2] = {NULL, NULL};
        size_t path_count = 0;
        for (argument_index = 2; argument_index < argc; ++argument_index) {
            if (!is_diagnostic_option(argv[argument_index]) &&
                path_count < 2)
                paths[path_count++] = argv[argument_index];
            else if (!is_diagnostic_option(argv[argument_index])) {
                cli_errorf("inspect", "CBS-E1001", "cli", 2,
                           "too many inspect paths or unknown option `%s`",
                           argv[argument_index]);
                return 2;
            }
        }
        if (path_count == 1)
            return inspect_file(paths[0], NULL);
        if (path_count == 2)
            return inspect_file(paths[0], paths[1]);
        cli_errorf("inspect", "CBS-E1001", "cli", 2,
                   "expected a recipe and optional artifact");
        return 2;
    }
    if (argc < 3 ||
        (strcmp(argv[1], "validate") != 0 && strcmp(argv[1], "check") != 0)) {
        usage(stderr);
        return 2;
    }
    return validate_file(argv[2]);
}
