#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"

Pipeline parse(TokenList list) {
    Pipeline pipeline;
    pipeline.command_count = 0;

    Command current;
    current.name = NULL;
    current.arg_count = 0;
    current.redirect_out = NULL;
    current.redirect_in = NULL;

    for (int i = 0; i < list.count; i++) {
        Token t = list.tokens[i];

        if (t.type == TOKEN_WORD) {
            if (current.name == NULL) {
                // first word is the command name
                current.name = strdup(t.value);
                current.args[0] = strdup(t.value);
                current.arg_count = 1;
            } else {
                // everything else is an argument
                current.args[current.arg_count++] = strdup(t.value);
            }
        }

        else if (t.type == TOKEN_REDIRECT_OUT) {
            // next word is the filename
            i++;
            if (list.tokens[i].type == TOKEN_WORD) {
                current.redirect_out = strdup(list.tokens[i].value);
            }
        }

        else if (t.type == TOKEN_REDIRECT_IN) {
            // next word is the filename
            i++;
            if (list.tokens[i].type == TOKEN_WORD) {
                current.redirect_in = strdup(list.tokens[i].value);
            }
        }

        else if (t.type == TOKEN_PIPE || t.type == TOKEN_EOF) {
            // save current command into pipeline
            if (current.name != NULL) {
                current.args[current.arg_count] = NULL; // null terminate args
                pipeline.commands[pipeline.command_count++] = current;

                // reset for next command
                current.name = NULL;
                current.arg_count = 0;
                current.redirect_out = NULL;
                current.redirect_in = NULL;
            }
        }
    }

    return pipeline;
}