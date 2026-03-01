#ifndef PARSER_H
#define PARSER_H

#include "lexer.h"

#define MAX_ARGS 64
#define MAX_COMMANDS 16

// a single command like "ls -la"
typedef struct {
    char *name;           // the command name, e.g. "ls"
    char *args[MAX_ARGS]; // the arguments, e.g. "-la"
    int arg_count;
    char *redirect_out;   // filename if > was used, otherwise NULL
    char *redirect_in;    // filename if < was used, otherwise NULL
} Command;

// a pipeline is one or more commands connected by pipes
typedef struct {
    Command commands[MAX_COMMANDS];
    int command_count;
} Pipeline;

Pipeline parse(TokenList list);

#endif