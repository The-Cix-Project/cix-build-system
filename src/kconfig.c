/* Deterministic, deliberately narrow Linux kconfig fragment merging. */
#include "cbs.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *name;
    char *state;
} ConfigEntry;

typedef struct {
    ConfigEntry *items;
    size_t count;
    size_t capacity;
} ConfigList;

static void list_destroy(ConfigList *list) {
    size_t index;
    for (index = 0; index < list->count; ++index) {
        free(list->items[index].name);
        free(list->items[index].state);
    }
    free(list->items);
}

static void set_error(char *error, size_t size, const char *message,
                      const char *path, size_t line) {
    if (error != NULL && size != 0)
        snprintf(error, size, "%s: %s:%zu", message, path, line);
}

static int valid_symbol(const char *name) {
    size_t index;
    if (strncmp(name, "CONFIG_", 7) != 0 || name[7] == '\0')
        return 0;
    for (index = 7; name[index] != '\0'; ++index)
        if (!(name[index] == '_' || isalnum((unsigned char)name[index])))
            return 0;
    return 1;
}

static int parse_line(char *line, char **name, char **state) {
    char *cursor = line;
    char *equals;
    char *end;
    while (isspace((unsigned char)*cursor))
        ++cursor;
    if (*cursor == '\0' || *cursor == '#') {
        if (strncmp(cursor, "# CONFIG_", 9) == 0) {
            *name = cursor + 2;
            end = strstr(*name, " is not set");
            if (end == NULL)
                return -1;
            *end = '\0';
            *state = "n";
            return valid_symbol(*name) ? 1 : -1;
        }
        return 0;
    }
    equals = strchr(cursor, '=');
    if (equals == NULL)
        return -1;
    *equals = '\0';
    end = equals - 1;
    while (end > cursor && isspace((unsigned char)*end))
        *end-- = '\0';
    *name = cursor;
    *state = equals + 1;
    end = *state + strlen(*state);
    while (end > *state && isspace((unsigned char)end[-1]))
        *--end = '\0';
    return valid_symbol(*name) &&
                   (strcmp(*state, "y") == 0 || strcmp(*state, "m") == 0 ||
                    strcmp(*state, "n") == 0)
               ? 1
               : -1;
}

static int apply_file(ConfigList *list, const char *path, char *error,
                      size_t error_size) {
    FILE *file = fopen(path, "r");
    char line[4096];
    size_t line_number = 0;
    if (file == NULL) {
        set_error(error, error_size, "cannot read kconfig", path, 0);
        return 0;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        char *name;
        char *state;
        size_t index;
        int parsed;
        ++line_number;
        parsed = parse_line(line, &name, &state);
        if (parsed < 0) {
            set_error(error, error_size, "unsupported kconfig line", path,
                      line_number);
            fclose(file);
            return 0;
        }
        if (parsed == 0)
            continue;
        for (index = 0; index < list->count; ++index)
            if (strcmp(list->items[index].name, name) == 0)
                break;
        if (index == list->count) {
            if (list->count == list->capacity) {
                size_t next = list->capacity == 0 ? 16 : list->capacity * 2;
                list->items = cbs_reallocate(list->items,
                                              next * sizeof(*list->items));
                list->capacity = next;
            }
            list->items[index].name = cbs_duplicate(name);
            list->items[index].state = NULL;
            ++list->count;
        }
        free(list->items[index].state);
        list->items[index].state = cbs_duplicate(state);
    }
    if (ferror(file)) {
        set_error(error, error_size, "cannot read kconfig", path, line_number);
        fclose(file);
        return 0;
    }
    fclose(file);
    return 1;
}

int cbs_kconfig_merge(const char *base, const char *fragment,
                      const char *output, char *error, size_t error_size) {
    ConfigList list = {0};
    FILE *file;
    size_t index;
    int result = 0;
    if (base == NULL || fragment == NULL || output == NULL)
        return 0;
    if (!apply_file(&list, base, error, error_size) ||
        !apply_file(&list, fragment, error, error_size))
        goto done;
    file = fopen(output, "w");
    if (file == NULL) {
        set_error(error, error_size, "cannot write kconfig", output, 0);
        goto done;
    }
    for (index = 0; index < list.count; ++index)
        if (fprintf(file, "%s=%s\n", list.items[index].name,
                    list.items[index].state) < 0) {
            fclose(file);
            goto done;
        }
    result = fclose(file) == 0;
done:
    list_destroy(&list);
    return result;
}
