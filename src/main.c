#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "executor.h"
#include "lexer.h"
#include "parser.h"

static void print_usage(void) {
    printf("OHSH - human-first shell\n\n");
    printf("Usage:\n");
    printf("  ohsh\n");
    printf("  ohsh run script.osh [--yes]\n");
    printf("  ohsh --help\n\n");
    printf("Options:\n");
    printf("  -y, --yes       allow destructive script actions without prompts\n");
    printf("  --no-color      disable ANSI color commands\n");
    printf("  --debug         keep extra diagnostic state enabled\n");
}

static int is_blank_or_comment(const char *line) {
    while (*line == ' ' || *line == '\t') line++;
    return *line == '\0' || *line == '#';
}

static ExecutionResult run_line(ShellContext *context, const char *input, int allow_tips) {
    char *expanded = expand_alias_line(context, input);
    const char *effective = expanded ? expanded : input;

    add_history(context, input);
    TokenList tokens = tokenize(effective);
    Pipeline pipeline = parse(tokens);
    ExecutionResult result = execute(pipeline, context);
    if (allow_tips && result == EXECUTION_CONTINUE && pipeline.command_count == 1) {
        maybe_print_tip(context, pipeline.commands[0].kind);
    }

    free_pipeline(&pipeline);
    free_tokens(tokens);
    free(expanded);
    return result;
}

static int run_script(ShellContext *context, const char *path) {
    FILE *file = fopen(path, "r");
    if (!file) {
        printf("I couldn't open script \"%s\".\n", path);
        return 1;
    }

    char input[4096];
    int line_number = 0;
    while (fgets(input, sizeof(input), file)) {
        line_number++;
        input[strcspn(input, "\r\n")] = '\0';
        if (is_blank_or_comment(input)) continue;

        ExecutionResult result = run_line(context, input, 0);
        if (result == EXECUTION_EXIT) break;
        if (context->last_status != 0) {
            printf("Script stopped at line %d.\n", line_number);
            fclose(file);
            return context->last_status;
        }
    }

    fclose(file);
    return context->last_status;
}

int main(int argc, char **argv) {
    char input[4096];
    ShellContext context;
    const char *script_path = NULL;

    init_shell_context(&context);
    load_shell_config(&context);

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage();
            free_shell_context(&context);
            return 0;
        } else if (strcmp(argv[i], "--no-color") == 0) {
            context.color_enabled = 0;
        } else if (strcmp(argv[i], "--debug") == 0) {
            context.debug_enabled = 1;
        } else if (strcmp(argv[i], "--yes") == 0 || strcmp(argv[i], "-y") == 0) {
            context.assume_yes = 1;
        } else if (strcmp(argv[i], "run") == 0 && i + 1 < argc) {
            script_path = argv[++i];
            context.non_interactive = 1;
        } else {
            printf("I don't understand option \"%s\".\n\n", argv[i]);
            print_usage();
            free_shell_context(&context);
            return 2;
        }
    }

    if (script_path) {
        int status = run_script(&context, script_path);
        free_shell_context(&context);
        return status;
    }

    print_welcome();

    while (1) {
        print_prompt();

        if (!fgets(input, sizeof(input), stdin)) break;

        input[strcspn(input, "\r\n")] = '\0';
        if (is_blank_or_comment(input)) continue;

        ExecutionResult result = run_line(&context, input, 1);
        if (result == EXECUTION_EXIT) break;
    }

    int status = context.last_status;
    free_shell_context(&context);
    return status == 0 ? 0 : 1;
}
