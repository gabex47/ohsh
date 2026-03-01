#ifndef LEXER_H
#define LEXER_H

// all the different types of tokens
typedef enum {
    TOKEN_WORD,        // a command or argument like "ls" or "-la"
    TOKEN_PIPE,        // |
    TOKEN_REDIRECT_OUT, // >
    TOKEN_REDIRECT_IN,  // 
    TOKEN_EOF          // end of input
} TokenType;

// a token has a type and sometimes a value
typedef struct {
    TokenType type;
    char *value; // only used for TOKEN_WORD
} Token;

// a list of tokens
typedef struct {
    Token *tokens;
    int count;
} TokenList;

TokenList tokenize(const char *input);
void free_tokens(TokenList list);

#endif