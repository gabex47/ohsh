#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include "executor.h"

// find the full path of a command like "ls" -> "/bin/ls"
char *resolve_command(const char *name) {
    if (name[0] == '/') return strdup(name);

    char *path_env = getenv("PATH");
    if (!path_env) return NULL;

    char *path = strdup(path_env);
    char *dir = strtok(path, ":");

    while (dir != NULL) {
        char full[1024];
        snprintf(full, sizeof(full), "%s/%s", dir, name);
        if (access(full, X_OK) == 0) {
            free(path);
            return strdup(full);
        }
        dir = strtok(NULL, ":");
    }

    free(path);
    return NULL;
}

void run_single(Command cmd) {
    // built in: help
    if (strcmp(cmd.name, "help") == 0) {
        printf("\n");
        printf("ohsh - available commands\n");
        printf("-------------------------\n");
        printf("cd [dir]        change directory. cd alone goes home\n");
        printf("echo [text]     print text to the screen\n");
        printf("exit            quit ohsh\n");
        printf("help            show this help message\n");
        printf("color [name]    change text color (red green blue yellow cyan magenta white reset)\n");
        printf("update          rebuild and reinstall ohsh with latest changes\n");
        printf("\n");
        printf("any other command (ls, cat, grep etc) runs the system version\n");
        printf("\n");
        return;
    }

    // built in: color
    if (strcmp(cmd.name, "color") == 0) {
        if (cmd.arg_count < 2) {
            printf("usage: color [red|green|blue|yellow|cyan|magenta|white|reset]\n");
            return;
        }
        char *color = cmd.args[1];
        if (strcmp(color, "red") == 0)          printf("\033[31m");
        else if (strcmp(color, "green") == 0)   printf("\033[32m");
        else if (strcmp(color, "yellow") == 0)  printf("\033[33m");
        else if (strcmp(color, "blue") == 0)    printf("\033[34m");
        else if (strcmp(color, "magenta") == 0) printf("\033[35m");
        else if (strcmp(color, "cyan") == 0)    printf("\033[36m");
        else if (strcmp(color, "white") == 0)   printf("\033[37m");
        else if (strcmp(color, "reset") == 0)   printf("\033[0m");
        else printf("ohsh: unknown color: %s\n", color);
        fflush(stdout);
        return;
    }

  // built in: update
if (strcmp(cmd.name, "update") == 0) {
    printf("checking for updates...\n");
    fflush(stdout);

    // get the download url of the latest release binary
    int result = system(
        "curl -s https://api.github.com/repos/gabex47/ohsh/releases/latest "
        "| grep browser_download_url "
        "| cut -d '\"' -f 4 "
        "> /tmp/ohsh_url.txt"
    );

    // read the url from the file
    FILE *f = fopen("/tmp/ohsh_url.txt", "r");
    if (!f) {
        printf("ohsh: could not check for updates\n");
        return;
    }

    char url[1024];
    if (!fgets(url, sizeof(url), f)) {
        printf("ohsh: no releases found\n");
        fclose(f);
        return;
    }
    fclose(f);

    // strip newline
    url[strcspn(url, "\n")] = 0;

    if (strlen(url) == 0) {
        printf("ohsh: no releases found\n");
        return;
    }

    printf("downloading latest release...\n");
    fflush(stdout);

    char download_cmd[2048];
    snprintf(download_cmd, sizeof(download_cmd),
        "curl -L -o /tmp/ohsh_new \"%s\" && chmod +x /tmp/ohsh_new", url);

    result = system(download_cmd);
    if (result != 0) {
        printf("ohsh: download failed\n");
        return;
    }

    printf("installing...\n");
    fflush(stdout);

    result = system("sudo cp /tmp/ohsh_new /usr/local/bin/ohsh");
    if (result != 0) {
        printf("ohsh: install failed\n");
        return;
    }

    printf("done! restart ohsh to use the latest version\n");
    return;
}
    // built in: cd
    if (strcmp(cmd.name, "cd") == 0) {
        char *dir = cmd.arg_count > 1 ? cmd.args[1] : getenv("HOME");
        if (chdir(dir) != 0) {
            perror("ohsh: cd");
        }
        return;
    }

    // built in: echo
    if (strcmp(cmd.name, "echo") == 0) {
        for (int i = 1; i < cmd.arg_count; i++) {
            printf("%s", cmd.args[i]);
            if (i < cmd.arg_count - 1) printf(" ");
        }
        printf("\n");
        return;
    }

    char *path = resolve_command(cmd.name);
    if (!path) {
        printf("ohsh: command not found: %s\n", cmd.name);
        return;
    }

    pid_t pid = fork();

    if (pid == 0) {
        if (cmd.redirect_out) {
            int fd = open(cmd.redirect_out, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }

        if (cmd.redirect_in) {
            int fd = open(cmd.redirect_in, O_RDONLY);
            dup2(fd, STDIN_FILENO);
            close(fd);
        }

        execv(path, cmd.args);
        perror("ohsh: execv");
        exit(1);

    } else {
        waitpid(pid, NULL, 0);
    }

    free(path);
}

void run_pipeline(Pipeline pipeline) {
    int num = pipeline.command_count;

    int pipes[num - 1][2];
    for (int i = 0; i < num - 1; i++) {
        pipe(pipes[i]);
    }

    for (int i = 0; i < num; i++) {
        pid_t pid = fork();

        if (pid == 0) {
            if (i > 0) {
                dup2(pipes[i - 1][0], STDIN_FILENO);
            }

            if (i < num - 1) {
                dup2(pipes[i][1], STDOUT_FILENO);
            }

            for (int j = 0; j < num - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            char *path = resolve_command(pipeline.commands[i].name);
            if (!path) {
                printf("ohsh: command not found: %s\n", pipeline.commands[i].name);
                exit(1);
            }

            execv(path, pipeline.commands[i].args);
            perror("ohsh: execv");
            exit(1);
        }
    }

    for (int i = 0; i < num - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    for (int i = 0; i < num; i++) {
        wait(NULL);
    }
}

void execute(Pipeline pipeline) {
    if (pipeline.command_count == 0) return;

    if (pipeline.command_count == 1) {
        run_single(pipeline.commands[0]);
    } else {
        run_pipeline(pipeline);
    }
}