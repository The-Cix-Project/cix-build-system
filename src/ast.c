#include "cbs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *cbs_allocate(size_t size)
{
    void *pointer = calloc(1, size);

    if (pointer == NULL) {
        fputs("cbs: fatal: memory allocation failed\n", stderr);
        exit(70);
    }
    return pointer;
}

void *cbs_reallocate(void *pointer, size_t size)
{
    void *result = realloc(pointer, size);

    if (result == NULL && size != 0) {
        fputs("cbs: fatal: memory allocation failed\n", stderr);
        exit(70);
    }
    return result;
}

char *cbs_duplicate_range(const char *start, size_t length)
{
    char *result = cbs_allocate(length + 1);

    memcpy(result, start, length);
    result[length] = '\0';
    return result;
}

char *cbs_duplicate(const char *text)
{
    return cbs_duplicate_range(text, strlen(text));
}

CbsNode *cbs_node_create(CbsNodeKind kind, CbsLocation location)
{
    CbsNode *node = cbs_allocate(sizeof(*node));

    node->kind = kind;
    node->location = location;
    node->number = -1;
    return node;
}

void cbs_node_add(CbsNode *parent, CbsNode *child)
{
    size_t capacity;

    if (parent->child_count == parent->child_capacity) {
        capacity = parent->child_capacity == 0 ? 8 : parent->child_capacity * 2;
        parent->children = cbs_reallocate(parent->children,
                                          capacity * sizeof(*parent->children));
        parent->child_capacity = capacity;
    }
    parent->children[parent->child_count++] = child;
}

void cbs_node_destroy(CbsNode *node)
{
    size_t index;

    if (node == NULL)
        return;
    for (index = 0; index < node->child_count; ++index)
        cbs_node_destroy(node->children[index]);
    free(node->children);
    free(node->name);
    free(node->value);
    free(node->second_value);
    free(node);
}

void cbs_token_list_destroy(CbsTokenList *tokens)
{
    size_t index;

    for (index = 0; index < tokens->count; ++index)
        free(tokens->items[index].text);
    free(tokens->items);
    tokens->items = NULL;
    tokens->count = 0;
    tokens->capacity = 0;
}
