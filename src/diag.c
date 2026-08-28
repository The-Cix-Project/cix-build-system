#include "cbs.h"

#include <stdio.h>
#include <string.h>

static const char *category_name(CbsDiagCategory category)
{
    switch (category) {
    case CBS_DIAG_LEX:
        return "lex";
    case CBS_DIAG_PARSE:
        return "parse";
    case CBS_DIAG_VALIDATION:
        return "validation";
    case CBS_DIAG_RUNTIME:
        return "runtime";
    case CBS_DIAG_INTERNAL:
        return "internal";
    }
    return "internal";
}

void cbs_diagnostic(const char *path, const char *source,
                    CbsLocation location, const char *severity,
                    const char *code, CbsDiagCategory category,
                    const char *message)
{
    const char *line_start;
    const char *line_end;
    size_t line_length;
    size_t prefix;

    fprintf(stderr, "%s:%lu:%lu: %s[%s]: %s: %s\n",
            path, (unsigned long)location.line, (unsigned long)location.column,
            severity, code, category_name(category), message);
    if (source == NULL)
        return;
    line_start = source + location.offset;
    while (line_start > source && line_start[-1] != '\n' && line_start[-1] != '\r')
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
                             const char *found)
{
    char message[512];

    snprintf(message, sizeof(message), "expected `%s`, found `%s`",
             expected, found);
    cbs_diagnostic(path, source, location, "error", "CPDL-E2001",
                   CBS_DIAG_PARSE, message);
}
