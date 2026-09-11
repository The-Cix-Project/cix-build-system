/* Structured diagnostics shared by the lexer, parser, validator, and runtime.
 */
#include "cbs.h"

#include <stdio.h>
#include <string.h>

static int json_diagnostics;

static void json_string(const char *text) {
    const unsigned char *cursor = (const unsigned char *)text;
    while (*cursor != '\0') {
        if (*cursor == '"' || *cursor == '\\')
            fputc('\\', stderr);
        if (*cursor == '\n')
            fputs("\\n", stderr);
        else if (*cursor == '\r')
            fputs("\\r", stderr);
        else if (*cursor == '\t')
            fputs("\\t", stderr);
        else if (*cursor >= 0x20)
            fputc(*cursor, stderr);
        cursor++;
    }
}

void cbs_diagnostic_set_json(int enabled) { json_diagnostics = enabled != 0; }

static const char *category_name(CbsDiagCategory category) {
    switch (category) {
    case CBS_DIAG_LEX:
        return "lex";
    case CBS_DIAG_PARSE:
        return "parse";
    case CBS_DIAG_VALIDATION:
        return "validation";
    case CBS_DIAG_RUNTIME:
        return "runtime";
    case CBS_DIAG_SOURCE:
        return "source";
    case CBS_DIAG_INTERNAL:
        return "internal";
    }
    return "internal";
}

void cbs_diagnostic(const char *path, const char *source, CbsLocation location,
                    const char *severity, const char *code,
                    CbsDiagCategory category, const char *message) {
    const char *line_start;
    const char *line_end;
    size_t line_length;
    size_t prefix;

    if (json_diagnostics) {
        fputs("{\"path\":\"", stderr);
        json_string(path);
        fprintf(stderr, "\",\"line\":%lu,\"column\":%lu,\"severity\":\"",
                (unsigned long)location.line, (unsigned long)location.column);
        json_string(severity);
        fputs("\",\"code\":\"", stderr);
        json_string(code);
        fputs("\",\"category\":\"", stderr);
        json_string(category_name(category));
        fputs("\",\"message\":\"", stderr);
        json_string(message);
        fputs("\"}\n", stderr);
        return;
    }

    fprintf(stderr, "%s:%lu:%lu: %s[%s]: %s: %s\n", path,
            (unsigned long)location.line, (unsigned long)location.column,
            severity, code, category_name(category), message);
    if (source == NULL)
        return;
    line_start = source + location.offset;
    while (line_start > source && line_start[-1] != '\n' &&
           line_start[-1] != '\r')
        --line_start;
    line_end = source + location.offset;
    while (*line_end != '\0' && *line_end != '\n' && *line_end != '\r')
        ++line_end;
    line_length = (size_t)(line_end - line_start);
    fprintf(stderr, "  %lu | %.*s\n", (unsigned long)location.line,
            (int)line_length, line_start);
    fprintf(stderr, "     | ");
    prefix = location.column > 0 ? location.column - 1 : 0;
    while (prefix-- > 0)
        fputc(' ', stderr);
    fputs("^\n", stderr);
}

void cbs_diagnostic_expected(const char *path, const char *source,
                             CbsLocation location, const char *expected,
                             const char *found) {
    char message[512];

    snprintf(message, sizeof(message), "expected `%s`, found `%s`", expected,
             found);
    cbs_diagnostic(path, source, location, "error", "CPDL-E2001",
                   CBS_DIAG_PARSE, message);
}
