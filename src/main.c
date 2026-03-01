#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lexer.h"
#include "parser.h"
#include "executor.h"

int main() {
    char input[1024];

    printf("ohsh 0.1 - type 'exit' to quit\n");

    while (1) {
        printf("ohsh > ");
        fflush(stdout);

        if (!fgets(input, 1024, stdin)) break;

        input[strcspn(input, "\n")] = 0;
        if (strlen(input) == 0) continue;

        if (strcmp(input, "exit") == 0) {
            printf("bye\n");
            break;
        }

        TokenList list = tokenize(input);
        Pipeline pipeline = parse(list);
        execute(pipeline);
        free_tokens(list);
    }

    return 0;
}