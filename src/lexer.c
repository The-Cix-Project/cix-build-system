/* UTF-8-aware lexical analysis for the CPDL source language. */
#include "cbs.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *path;
    const char *source;
    size_t length;
    size_t offset;
    size_t line;
    size_t column;
    CbsTokenList *tokens;
    int failed;
} Lexer;

/* Return the source location at the current lexer cursor. */
static CbsLocation location(const Lexer *lexer) {
    CbsLocation result;

    result.path = lexer->path;
    result.line = lexer->line;
    result.column = lexer->column;
    result.offset = lexer->offset;
    return result;
}

/* Append one token to the lexer's growing token list. */
static void token_add(Lexer *lexer, CbsTokenKind kind, char *text,
                      CbsLocation token_location) {
    size_t capacity;
    CbsToken *token;

    if (lexer->tokens->count == lexer->tokens->capacity) {
        capacity =
            lexer->tokens->capacity == 0 ? 64 : lexer->tokens->capacity * 2;
        lexer->tokens->items = cbs_reallocate(
            lexer->tokens->items, capacity * sizeof(*lexer->tokens->items));
        lexer->tokens->capacity = capacity;
    }
    token = &lexer->tokens->items[lexer->tokens->count++];
    token->kind = kind;
    token->text = text;
    token->location = token_location;
}

/* Emit a lexical diagnostic and stop accepting further input. */
static void lexical_error(Lexer *lexer, CbsLocation error_location,
                          const char *code, const char *message) {
    cbs_diagnostic(lexer->path, lexer->source, error_location, "error", code,
                   CBS_DIAG_LEX, message);
    lexer->failed = 1;
}

/* Return the expected byte width for a UTF-8 leading byte. */
static int utf8_width(unsigned char byte) {
    if (byte < 0x80)
        return 1;
    if (byte >= 0xc2 && byte <= 0xdf)
        return 2;
    if (byte >= 0xe0 && byte <= 0xef)
        return 3;
    if (byte >= 0xf0 && byte <= 0xf4)
        return 4;
    return 0;
}

/* Validate continuation bytes and Unicode scalar boundaries. */
static int valid_utf8_sequence(const char *text, size_t remaining, int width) {
    unsigned char first;
    unsigned char second;
    int index;

    if ((size_t)width > remaining)
        return 0;
    first = (unsigned char)text[0];
    second = width > 1 ? (unsigned char)text[1] : 0;
    if (width == 3 && first == 0xe0 && second < 0xa0)
        return 0;
    if (width == 3 && first == 0xed && second >= 0xa0)
        return 0;
    if (width == 4 && first == 0xf0 && second < 0x90)
        return 0;
    if (width == 4 && first == 0xf4 && second >= 0x90)
        return 0;
    for (index = 1; index < width; ++index) {
        unsigned char byte = (unsigned char)text[index];
        if (byte < 0x80 || byte > 0xbf)
            return 0;
    }
    return 1;
}

/* Reject malformed UTF-8 and embedded NUL bytes before tokenization. */
static int validate_source_utf8(Lexer *lexer) {
    size_t offset = 0;
    size_t line = 1;
    size_t column = 1;
    CbsLocation error_location;

    while (offset < lexer->length) {
        unsigned char byte = (unsigned char)lexer->source[offset];
        int width;

        if (byte == 0) {
            error_location = location(lexer);
            error_location.offset = offset;
            error_location.line = line;
            error_location.column = column;
            lexical_error(lexer, error_location, "CPDL-E1001",
                          "NUL is not permitted in CPDL source");
            return 0;
        }
        width = utf8_width(byte);
        if (width == 0 || !valid_utf8_sequence(lexer->source + offset,
                                               lexer->length - offset, width)) {
            error_location = location(lexer);
            error_location.offset = offset;
            error_location.line = line;
            error_location.column = column;
            lexical_error(lexer, error_location, "CPDL-E1001",
                          "source is not valid UTF-8");
            return 0;
        }
        if (byte == '\n') {
            ++line;
            column = 1;
        } else if (byte == '\t') {
            column = ((column - 1) / 8 + 1) * 8 + 1;
        } else {
            ++column;
        }
        offset += (size_t)width;
    }
    return 1;
}

/* Advance one ASCII byte and update line and column state. */
static void advance_ascii(Lexer *lexer) {
    char character = lexer->source[lexer->offset++];

    if (character == '\n') {
        ++lexer->line;
        lexer->column = 1;
    } else if (character == '\t') {
        lexer->column = ((lexer->column - 1) / 8 + 1) * 8 + 1;
    } else {
        ++lexer->column;
    }
}

/* Advance over one validated UTF-8 scalar value. */
static void advance_scalar(Lexer *lexer) {
    int width = utf8_width((unsigned char)lexer->source[lexer->offset]);

    if (width <= 1) {
        advance_ascii(lexer);
        return;
    }
    lexer->offset += (size_t)width;
    ++lexer->column;
}

/* Test whether the lexer has consumed every source byte. */
static int at_end(const Lexer *lexer) { return lexer->offset >= lexer->length; }

/* Test whether a character may begin an unquoted CPDL word. */
static int is_word_start(char character) {
    return (character >= 'A' && character <= 'Z') ||
           (character >= 'a' && character <= 'z') || character == '_';
}

/* Test whether a character may continue an unquoted CPDL word. */
static int is_word_continue(char character) {
    return is_word_start(character) || (character >= '0' && character <= '9') ||
           character == '-';
}

/* Convert one hexadecimal digit to its numeric value. */
static int hex_value(char character) {
    if (character >= '0' && character <= '9')
        return character - '0';
    if (character >= 'a' && character <= 'f')
        return character - 'a' + 10;
    if (character >= 'A' && character <= 'F')
        return character - 'A' + 10;
    return -1;
}

/* Append one decoded byte to a growing lexer buffer. */
static void append_byte(char **buffer, size_t *length, size_t *capacity,
                        unsigned char byte) {
    if (*length + 1 >= *capacity) {
        *capacity = *capacity == 0 ? 32 : *capacity * 2;
        *buffer = cbs_reallocate(*buffer, *capacity);
    }
    (*buffer)[(*length)++] = (char)byte;
}

/* Encode one Unicode scalar as UTF-8 in a growing buffer. */
static void append_codepoint(char **buffer, size_t *length, size_t *capacity,
                             unsigned long codepoint) {
    if (codepoint <= 0x7f) {
        append_byte(buffer, length, capacity, (unsigned char)codepoint);
    } else if (codepoint <= 0x7ff) {
        append_byte(buffer, length, capacity,
                    (unsigned char)(0xc0 | (codepoint >> 6)));
        append_byte(buffer, length, capacity,
                    (unsigned char)(0x80 | (codepoint & 0x3f)));
    } else {
        append_byte(buffer, length, capacity,
                    (unsigned char)(0xe0 | (codepoint >> 12)));
        append_byte(buffer, length, capacity,
                    (unsigned char)(0x80 | ((codepoint >> 6) & 0x3f)));
        append_byte(buffer, length, capacity,
                    (unsigned char)(0x80 | (codepoint & 0x3f)));
    }
}

/* Lex a quoted string and decode its escape sequences. */
static void lex_string(Lexer *lexer) {
    CbsLocation start = location(lexer);
    char *buffer = NULL;
    size_t length = 0;
    size_t capacity = 0;

    advance_ascii(lexer);
    while (!at_end(lexer) && lexer->source[lexer->offset] != '"') {
        unsigned char character = (unsigned char)lexer->source[lexer->offset];

        if (character == '\n' || character == '\r' || character < 0x20) {
            lexical_error(lexer, location(lexer), "CPDL-E1003",
                          "control character in quoted string");
            free(buffer);
            return;
        }
        if (character == '\\') {
            int value;
            unsigned long codepoint = 0;
            int digits;
            int index;

            advance_ascii(lexer);
            if (at_end(lexer))
                break;
            character = (unsigned char)lexer->source[lexer->offset];
            advance_ascii(lexer);
            if (character == '"' || character == '\\')
                append_byte(&buffer, &length, &capacity, character);
            else if (character == 'n')
                append_byte(&buffer, &length, &capacity, '\n');
            else if (character == 'r')
                append_byte(&buffer, &length, &capacity, '\r');
            else if (character == 't')
                append_byte(&buffer, &length, &capacity, '\t');
            else if (character == '0') {
                lexical_error(lexer, start, "CPDL-E1003",
                              "NUL escape is not permitted in a string");
                free(buffer);
                return;
            } else if (character == 'x' || character == 'u') {
                digits = character == 'x' ? 2 : 4;
                for (index = 0; index < digits; ++index) {
                    if (at_end(lexer) ||
                        (value = hex_value(lexer->source[lexer->offset])) < 0) {
                        lexical_error(lexer, start, "CPDL-E1003",
                                      "invalid hexadecimal string escape");
                        free(buffer);
                        return;
                    }
                    codepoint = codepoint * 16 + (unsigned long)value;
                    advance_ascii(lexer);
                }
                if (codepoint == 0 ||
                    (codepoint >= 0xd800 && codepoint <= 0xdfff)) {
                    lexical_error(lexer, start, "CPDL-E1003",
                                  "invalid Unicode string escape");
                    free(buffer);
                    return;
                }
                append_codepoint(&buffer, &length, &capacity, codepoint);
            } else {
                lexical_error(lexer, start, "CPDL-E1003",
                              "unknown string escape");
                free(buffer);
                return;
            }
        } else {
            int width = utf8_width(character);
            int index;

            for (index = 0; index < width; ++index)
                append_byte(&buffer, &length, &capacity,
                            (unsigned char)
                                lexer->source[lexer->offset + (size_t)index]);
            advance_scalar(lexer);
        }
    }
    if (at_end(lexer)) {
        lexical_error(lexer, start, "CPDL-E1003", "unterminated quoted string");
        free(buffer);
        return;
    }
    advance_ascii(lexer);
    append_byte(&buffer, &length, &capacity, 0);
    token_add(lexer, CBS_TOKEN_STRING, buffer, start);
}

/* Find the closing indentation line of a block string. */
static int block_closing_line(const Lexer *lexer, size_t offset, size_t *indent,
                              size_t *after) {
    size_t cursor = offset;

    while (cursor < lexer->length &&
           (lexer->source[cursor] == ' ' || lexer->source[cursor] == '\t'))
        ++cursor;
    *indent = cursor - offset;
    if (cursor + 3 > lexer->length ||
        memcmp(lexer->source + cursor, "\"\"\"", 3) != 0)
        return 0;
    cursor += 3;
    while (cursor < lexer->length &&
           (lexer->source[cursor] == ' ' || lexer->source[cursor] == '\t'))
        ++cursor;
    if (cursor < lexer->length && lexer->source[cursor] != '\n')
        return 0;
    *after = cursor < lexer->length ? cursor + 1 : cursor;
    return 1;
}

/* Lex a multiline block string while preserving its content. */
static void lex_block_string(Lexer *lexer) {
    CbsLocation start = location(lexer);
    size_t content_start;
    size_t cursor;
    size_t closing_offset = 0;
    size_t closing_indent = 0;
    size_t after = 0;
    size_t output_length = 0;
    size_t output_capacity = 0;
    char *output = NULL;

    advance_ascii(lexer);
    advance_ascii(lexer);
    advance_ascii(lexer);
    if (at_end(lexer) || lexer->source[lexer->offset] != '\n') {
        lexical_error(lexer, start, "CPDL-E1003",
                      "block string opener must be followed by a newline");
        return;
    }
    advance_ascii(lexer);
    content_start = lexer->offset;
    cursor = content_start;
    while (cursor < lexer->length) {
        size_t line_end = cursor;

        if (block_closing_line(lexer, cursor, &closing_indent, &after)) {
            closing_offset = cursor;
            break;
        }
        while (line_end < lexer->length && lexer->source[line_end] != '\n')
            ++line_end;
        cursor = line_end < lexer->length ? line_end + 1 : line_end;
    }
    if (closing_offset == 0) {
        lexical_error(lexer, start, "CPDL-E1003", "unterminated block string");
        return;
    }
    cursor = content_start;
    while (cursor < closing_offset) {
        size_t line_end = cursor;
        size_t indent = 0;
        size_t content_offset;
        size_t index;

        while (line_end < closing_offset && lexer->source[line_end] != '\n')
            ++line_end;
        while (cursor + indent < line_end &&
               (lexer->source[cursor + indent] == ' ' ||
                lexer->source[cursor + indent] == '\t'))
            ++indent;
        content_offset = cursor + indent;
        if (content_offset < line_end && indent < closing_indent) {
            lexical_error(
                lexer, start, "CPDL-E1003",
                "block string line has less indentation than its closer");
            free(output);
            return;
        }
        if (content_offset == line_end)
            content_offset = line_end;
        else
            content_offset = cursor + closing_indent;
        for (index = content_offset; index < line_end; ++index)
            append_byte(&output, &output_length, &output_capacity,
                        (unsigned char)lexer->source[index]);
        if (line_end < closing_offset)
            append_byte(&output, &output_length, &output_capacity, '\n');
        cursor = line_end < closing_offset ? line_end + 1 : line_end;
    }
    while (lexer->offset < after)
        advance_scalar(lexer);
    append_byte(&output, &output_length, &output_capacity, 0);
    token_add(lexer, CBS_TOKEN_BLOCK_STRING, output, start);
}

/* Lex integer, duration, and file-mode literals. */
static void lex_number(Lexer *lexer) {
    CbsLocation start = location(lexer);
    size_t begin = lexer->offset;
    size_t digit_end;
    CbsTokenKind kind = CBS_TOKEN_INTEGER;
    char *text;
    char *end;
    long value;

    while (!at_end(lexer) &&
           isdigit((unsigned char)lexer->source[lexer->offset]))
        advance_ascii(lexer);
    digit_end = lexer->offset;
    if (!at_end(lexer) && lexer->source[lexer->offset] == 'm' &&
        lexer->offset + 1 < lexer->length &&
        lexer->source[lexer->offset + 1] == 's') {
        advance_ascii(lexer);
        advance_ascii(lexer);
        kind = CBS_TOKEN_DURATION;
    } else if (!at_end(lexer) && (lexer->source[lexer->offset] == 's' ||
                                  lexer->source[lexer->offset] == 'm' ||
                                  lexer->source[lexer->offset] == 'h')) {
        advance_ascii(lexer);
        kind = CBS_TOKEN_DURATION;
    } else if (lexer->source[begin] == '0' && lexer->offset - begin >= 4) {
        kind = CBS_TOKEN_MODE;
    }
    if (digit_end - begin > 1 && lexer->source[begin] == '0' &&
        kind != CBS_TOKEN_MODE) {
        lexical_error(lexer, start, "CPDL-E1002",
                      "leading zero is not permitted in a decimal value");
        return;
    }
    text = cbs_duplicate_range(lexer->source + begin, digit_end - begin);
    errno = 0;
    value = strtol(text, &end, 10);
    if (errno == ERANGE || *end != '\0' || value > INT_MAX) {
        free(text);
        lexical_error(lexer, start, "CPDL-E1002",
                      "integer exceeds the CPDL 0.1 range");
        return;
    }
    free(text);
    token_add(lexer, kind,
              cbs_duplicate_range(lexer->source + begin, lexer->offset - begin),
              start);
}

/* Lex one keyword or identifier-like word. */
static void lex_word(Lexer *lexer) {
    CbsLocation start = location(lexer);
    size_t begin = lexer->offset;

    while (!at_end(lexer) && is_word_continue(lexer->source[lexer->offset]))
        advance_ascii(lexer);
    token_add(lexer, CBS_TOKEN_WORD,
              cbs_duplicate_range(lexer->source + begin, lexer->offset - begin),
              start);
}

/* Lex an interpolation value beginning with `${`. */
static void lex_cbs_value(Lexer *lexer) {
    CbsLocation start = location(lexer);
    size_t begin = lexer->offset;

    advance_ascii(lexer);
    if (at_end(lexer) || !is_word_start(lexer->source[lexer->offset])) {
        lexical_error(lexer, start, "CPDL-E1002", "invalid CBS value");
        return;
    }
    while (!at_end(lexer) && is_word_continue(lexer->source[lexer->offset]))
        advance_ascii(lexer);
    if (!at_end(lexer) && lexer->source[lexer->offset] == '.') {
        advance_ascii(lexer);
        if (at_end(lexer) || !is_word_start(lexer->source[lexer->offset])) {
            lexical_error(lexer, start, "CPDL-E1002",
                          "invalid named CBS value");
            return;
        }
        while (!at_end(lexer) && is_word_continue(lexer->source[lexer->offset]))
            advance_ascii(lexer);
    }
    token_add(lexer, CBS_TOKEN_CBS_VALUE,
              cbs_duplicate_range(lexer->source + begin, lexer->offset - begin),
              start);
}

/* Tokenize a complete CPDL source string. */
int cbs_lex(const char *path, const char *source, size_t length,
            CbsTokenList *tokens) {
    Lexer lexer;

    memset(&lexer, 0, sizeof(lexer));
    lexer.path = path;
    lexer.source = source;
    lexer.length = length;
    lexer.line = 1;
    lexer.column = 1;
    lexer.tokens = tokens;
    if (!validate_source_utf8(&lexer))
        return 0;
    if (length >= 3 && (unsigned char)source[0] == 0xef &&
        (unsigned char)source[1] == 0xbb && (unsigned char)source[2] == 0xbf) {
        lexical_error(&lexer, location(&lexer), "CPDL-E1001",
                      "UTF-8 byte-order mark is not permitted");
        return 0;
    }
    while (!at_end(&lexer) && !lexer.failed) {
        char character = source[lexer.offset];
        CbsLocation start = location(&lexer);

        if (character == ' ' || character == '\t' || character == '\n') {
            advance_ascii(&lexer);
        } else if (character == '\r') {
            if (lexer.offset + 1 >= length ||
                source[lexer.offset + 1] != '\n') {
                lexical_error(&lexer, start, "CPDL-E1001",
                              "bare carriage return is not permitted");
            } else {
                ++lexer.offset;
                advance_ascii(&lexer);
            }
        } else if (character == '#') {
            while (!at_end(&lexer) && source[lexer.offset] != '\n')
                advance_scalar(&lexer);
        } else if (character == '"') {
            if (lexer.offset + 2 < length && source[lexer.offset + 1] == '"' &&
                source[lexer.offset + 2] == '"')
                lex_block_string(&lexer);
            else
                lex_string(&lexer);
        } else if (character == '{' || character == '}' || character == '=') {
            CbsTokenKind kind = character == '{'   ? CBS_TOKEN_LBRACE
                                : character == '}' ? CBS_TOKEN_RBRACE
                                                   : CBS_TOKEN_EQUAL;
            char text[2];
            text[0] = character;
            text[1] = '\0';
            advance_ascii(&lexer);
            token_add(&lexer, kind, cbs_duplicate(text), start);
        } else if (character >= '0' && character <= '9') {
            lex_number(&lexer);
        } else if (is_word_start(character)) {
            lex_word(&lexer);
        } else if (character == '$') {
            lex_cbs_value(&lexer);
        } else {
            lexical_error(&lexer, start, "CPDL-E1002",
                          "invalid character in CPDL source");
        }
    }
    if (lexer.failed)
        return 0;
    token_add(&lexer, CBS_TOKEN_EOF, cbs_duplicate("end of file"),
              location(&lexer));
    return 1;
}
