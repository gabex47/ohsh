#ifndef LEXER_H
#define LEXER_H

typedef enum {
    TOKEN_WORD,
    TOKEN_PIPE,
    TOKEN_REDIRECT_OUT,
    TOKEN_REDIRECT_APPEND,
    TOKEN_REDIRECT_IN,
    TOKEN_EOF
} TokenType;

typedef struct {
    TokenType type;
    char *value;
} Token;

typedef struct {
    Token *tokens;
    int count;
    char *error;
} TokenList;

TokenList tokenize(const char *input);
void free_tokens(TokenList list);

#endif
