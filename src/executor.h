#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "parser.h"

#define OHSH_HISTORY_MAX 200
#define OHSH_ALIAS_MAX 64

typedef struct {
    char *phrase;
    char *replacement;
} OhshAlias;

typedef struct {
    char *items[OHSH_HISTORY_MAX];
    int count;
    int commands_since_tip;
    int next_tip;
    int tips_enabled;
    int confirm_destructive;
    int color_enabled;
    int non_interactive;
    int assume_yes;
    int debug_enabled;
    int last_status;
    char *fallback_shell;
    OhshAlias aliases[OHSH_ALIAS_MAX];
    int alias_count;
} ShellContext;

typedef enum {
    EXECUTION_CONTINUE,
    EXECUTION_EXIT
} ExecutionResult;

void init_shell_context(ShellContext *context);
void load_shell_config(ShellContext *context);
char *expand_alias_line(const ShellContext *context, const char *line);
void add_history(ShellContext *context, const char *line);
void free_shell_context(ShellContext *context);
void print_welcome(void);
void print_prompt(void);
void maybe_print_tip(ShellContext *context, CommandKind kind);

ExecutionResult execute(Pipeline pipeline, ShellContext *context);

#endif
