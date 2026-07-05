#include <stdio.h>
#include <string.h>
#include "executor.h"
#include "lexer.h"
#include "parser.h"

int main(void) {
    char input[1024];
    ShellContext context;

    init_shell_context(&context);
    print_welcome();

    while (1) {
        print_prompt();

        if (!fgets(input, sizeof(input), stdin)) break;

        input[strcspn(input, "\n")] = '\0';
        if (input[0] == '\0') continue;

        add_history(&context, input);

        TokenList tokens = tokenize(input);
        Pipeline pipeline = parse(tokens);
        ExecutionResult result = execute(pipeline, &context);
        if (result == EXECUTION_CONTINUE && pipeline.command_count == 1) {
            maybe_print_tip(&context, pipeline.commands[0].kind);
        }

        free_pipeline(&pipeline);
        free_tokens(tokens);

        if (result == EXECUTION_EXIT) break;
    }

    free_shell_context(&context);
    return 0;
}
