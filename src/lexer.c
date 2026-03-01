#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "lexer.h"

#define MAX_TOKENS 256
#define MAX_WORD 256

TokenList tokenize(const char *input) {
    TokenList list;
    list.tokens = malloc(sizeof(Token) * MAX_TOKENS);
    list.count = 0;

    int i = 0;
    while (input[i] != '\0') {
        // skip spaces
        if (isspace(input[i])) {
            i++;
            continue;
        }

        // pipe
        if (input[i] == '|') {
            list.tokens[list.count].type = TOKEN_PIPE;
            list.tokens[list.count].value = NULL;
            list.count++;
            i++;
            continue;
        }

        // redirect out
        if (input[i] == '>') {
            list.tokens[list.count].type = TOKEN_REDIRECT_OUT;
            list.tokens[list.count].value = NULL;
            list.count++;
            i++;
            continue;
        }

        // redirect in
        if (input[i] == '<') {
            list.tokens[list.count].type = TOKEN_REDIRECT_IN;
            list.tokens[list.count].value = NULL;
            list.count++;
            i++;
            continue;
        }

        // word
        char word[MAX_WORD];
        int j = 0;
        while (input[i] != '\0' && !isspace(input[i]) &&
               input[i] != '|' && input[i] != '>' && input[i] != '<') {
            word[j++] = input[i++];
        }
        word[j] = '\0';

        list.tokens[list.count].type = TOKEN_WORD;
        list.tokens[list.count].value = strdup(word);
        list.count++;
    }

    // end of input
    list.tokens[list.count].type = TOKEN_EOF;
    list.tokens[list.count].value = NULL;
    list.count++;

    return list;
}

void free_tokens(TokenList list) {
    for (int i = 0; i < list.count; i++) {
        if (list.tokens[i].value) free(list.tokens[i].value);
    }
    free(list.tokens);
}