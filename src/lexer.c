#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lexer.h"

#define INITIAL_TOKEN_CAPACITY 32
#define INITIAL_WORD_CAPACITY 32

static char *dup_string(const char *value) {
    size_t length = strlen(value) + 1;
    char *copy = malloc(length);
    if (!copy) return NULL;
    memcpy(copy, value, length);
    return copy;
}

static int grow_tokens(TokenList *list, int *capacity) {
    if (list->count < *capacity) return 1;

    int next_capacity = *capacity * 2;
    Token *next = realloc(list->tokens, sizeof(Token) * (size_t)next_capacity);
    if (!next) return 0;

    list->tokens = next;
    *capacity = next_capacity;
    return 1;
}

static int add_token(TokenList *list, int *capacity, TokenType type, const char *value) {
    if (!grow_tokens(list, capacity)) return 0;

    list->tokens[list->count].type = type;
    list->tokens[list->count].value = value ? dup_string(value) : NULL;
    if (value && !list->tokens[list->count].value) return 0;

    list->count++;
    return 1;
}

static int append_char(char **word, int *length, int *capacity, char ch) {
    if (*length + 1 >= *capacity) {
        int next_capacity = *capacity * 2;
        char *next = realloc(*word, (size_t)next_capacity);
        if (!next) return 0;
        *word = next;
        *capacity = next_capacity;
    }

    (*word)[(*length)++] = ch;
    (*word)[*length] = '\0';
    return 1;
}

static char *read_word(const char *input, int *index, char **error) {
    int capacity = INITIAL_WORD_CAPACITY;
    int length = 0;
    char *word = malloc((size_t)capacity);
    if (!word) return NULL;
    word[0] = '\0';

    while (input[*index] != '\0' &&
           !isspace((unsigned char)input[*index]) &&
           input[*index] != '|' &&
           input[*index] != '>' &&
           input[*index] != '<') {
        if (input[*index] == '"' || input[*index] == '\'') {
            char quote = input[*index];
            (*index)++;

            while (input[*index] != '\0' && input[*index] != quote) {
                if (quote == '"' && input[*index] == '\\' && input[*index + 1] != '\0') {
                    (*index)++;
                }

                if (!append_char(&word, &length, &capacity, input[*index])) {
                    free(word);
                    return NULL;
                }
                (*index)++;
            }

            if (input[*index] != quote) {
                free(*error);
                *error = dup_string("I found a quote that was never closed.");
                break;
            }

            (*index)++;
            continue;
        }

        if (input[*index] == '\\' && input[*index + 1] != '\0') {
            (*index)++;
        }

        if (!append_char(&word, &length, &capacity, input[*index])) {
            free(word);
            return NULL;
        }
        (*index)++;
    }

    return word;
}

TokenList tokenize(const char *input) {
    TokenList list;
    int capacity = INITIAL_TOKEN_CAPACITY;

    list.tokens = malloc(sizeof(Token) * (size_t)capacity);
    list.count = 0;
    list.error = NULL;
    list.source = dup_string(input ? input : "");

    if (!list.tokens) {
        list.error = dup_string("OHSH ran out of memory while reading that command.");
        return list;
    }

    int i = 0;
    while (input[i] != '\0') {
        if (isspace((unsigned char)input[i])) {
            i++;
            continue;
        }

        if (input[i] == '|') {
            if (!add_token(&list, &capacity, TOKEN_PIPE, NULL)) goto memory_error;
            i++;
            continue;
        }

        if (input[i] == '>') {
            if (input[i + 1] == '>') {
                if (!add_token(&list, &capacity, TOKEN_REDIRECT_APPEND, NULL)) goto memory_error;
                i += 2;
            } else {
                if (!add_token(&list, &capacity, TOKEN_REDIRECT_OUT, NULL)) goto memory_error;
                i++;
            }
            continue;
        }

        if (input[i] == '<') {
            if (!add_token(&list, &capacity, TOKEN_REDIRECT_IN, NULL)) goto memory_error;
            i++;
            continue;
        }

        char *word = read_word(input, &i, &list.error);
        if (!word) goto memory_error;
        if (!add_token(&list, &capacity, TOKEN_WORD, word)) {
            free(word);
            goto memory_error;
        }
        free(word);
    }

    if (!add_token(&list, &capacity, TOKEN_EOF, NULL)) goto memory_error;
    return list;

memory_error:
    free_tokens(list);
    list.tokens = NULL;
    list.count = 0;
    list.error = dup_string("OHSH ran out of memory while reading that command.");
    return list;
}

void free_tokens(TokenList list) {
    for (int i = 0; i < list.count; i++) {
        free(list.tokens[i].value);
    }
    free(list.tokens);
    free(list.error);
    free(list.source);
}
