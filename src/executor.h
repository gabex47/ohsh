#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "parser.h"

#define OHSH_HISTORY_MAX 200

typedef struct {
    char *items[OHSH_HISTORY_MAX];
    int count;
    int commands_since_tip;
    int next_tip;
    int tips_enabled;
} ShellContext;

typedef enum {
    EXECUTION_CONTINUE,
    EXECUTION_EXIT
} ExecutionResult;

void init_shell_context(ShellContext *context);
void add_history(ShellContext *context, const char *line);
void free_shell_context(ShellContext *context);
void print_welcome(void);
void print_prompt(void);
void maybe_print_tip(ShellContext *context, CommandKind kind);

ExecutionResult execute(Pipeline pipeline, ShellContext *context);

#endif
