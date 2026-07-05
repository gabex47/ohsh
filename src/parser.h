#ifndef PARSER_H
#define PARSER_H

#include <stddef.h>
#include "lexer.h"

#define MAX_ARGS 64
#define MAX_COMMANDS 16

typedef enum {
    COMMAND_EMPTY,
    COMMAND_EXTERNAL,
    COMMAND_HELP,
    COMMAND_EXIT,
    COMMAND_CHANGE_DIR,
    COMMAND_PRINT_TEXT,
    COMMAND_WHERE_AM_I,
    COMMAND_LIST,
    COMMAND_MAKE_FOLDER,
    COMMAND_MAKE_FILE,
    COMMAND_DELETE_PATH,
    COMMAND_COPY_PATH,
    COMMAND_MOVE_PATH,
    COMMAND_READ_FILE,
    COMMAND_CLEAR_SCREEN,
    COMMAND_SHOW_HISTORY,
    COMMAND_EXAMPLES,
    COMMAND_COLOR,
    COMMAND_UPDATE,
    COMMAND_UNKNOWN
} CommandKind;

typedef enum {
    LIST_FILES,
    LIST_FOLDERS,
    LIST_EVERYTHING
} ListMode;

typedef struct {
    CommandKind kind;

    char *name;
    char *args[MAX_ARGS];
    int arg_count;

    char *target;
    char *source;
    char *destination;
    char *text;
    char *color;
    char *filter_extension;
    char *error;

    ListMode list_mode;
    int include_hidden;
    int has_min_size;
    size_t min_size_bytes;

    int recursive;
    int bulk;

    char *redirect_out;
    char *redirect_in;
    int redirect_append;
} Command;

typedef struct {
    Command commands[MAX_COMMANDS];
    int command_count;
} Pipeline;

Pipeline parse(TokenList list);
void free_pipeline(Pipeline *pipeline);
const char *command_kind_name(CommandKind kind);

#endif
