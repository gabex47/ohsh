#include "platform.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

struct OhshRedirectState {
    int saved_stdin;
    int saved_stdout;
};

static char last_error[256];

static void set_error_from_errno(void) {
    snprintf(last_error, sizeof(last_error), "%s", strerror(errno));
}

static char *dup_string(const char *value) {
    size_t length = strlen(value) + 1;
    char *copy = malloc(length);
    if (!copy) return NULL;
    memcpy(copy, value, length);
    return copy;
}

static char *join_path(const char *left, const char *right) {
    if (!right) return NULL;
    if (right[0] == '/') return dup_string(right);
    if (!left || left[0] == '\0' || strcmp(left, ".") == 0) return dup_string(right);

    size_t left_len = strlen(left);
    size_t right_len = strlen(right);
    int needs_slash = left[left_len - 1] != '/';
    char *joined = malloc(left_len + right_len + (needs_slash ? 2 : 1));
    if (!joined) return NULL;

    strcpy(joined, left);
    if (needs_slash) strcat(joined, "/");
    strcat(joined, right);
    return joined;
}

int ohsh_cd(const char *path) {
    if (chdir(path) != 0) {
        set_error_from_errno();
        return -1;
    }
    return 0;
}

int ohsh_mkdir(const char *path) {
    if (mkdir(path, 0755) != 0) {
        set_error_from_errno();
        return -1;
    }
    return 0;
}

int ohsh_delete_file(const char *path) {
    if (remove(path) != 0) {
        set_error_from_errno();
        return -1;
    }
    return 0;
}

int ohsh_delete_folder(const char *path) {
    if (rmdir(path) != 0) {
        set_error_from_errno();
        return -1;
    }
    return 0;
}

int ohsh_rename(const char *oldpath, const char *newpath) {
    if (rename(oldpath, newpath) != 0) {
        set_error_from_errno();
        return -1;
    }
    return 0;
}

char *ohsh_get_cwd(void) {
    char *cwd = getcwd(NULL, 0);
    if (!cwd) set_error_from_errno();
    return cwd;
}

char *ohsh_get_home(void) {
    const char *home = getenv("HOME");
    return home ? dup_string(home) : NULL;
}

OhshPathType ohsh_path_type(const char *path) {
    struct stat info;
    if (stat(path, &info) != 0) {
        set_error_from_errno();
        return OHSH_PATH_MISSING;
    }
    if (S_ISREG(info.st_mode)) return OHSH_PATH_FILE;
    if (S_ISDIR(info.st_mode)) return OHSH_PATH_FOLDER;
    return OHSH_PATH_OTHER;
}

int ohsh_is_same_path(const char *left, const char *right) {
    char left_real[PATH_MAX];
    char right_real[PATH_MAX];
    if (!realpath(left, left_real) || !realpath(right, right_real)) return 0;
    return strcmp(left_real, right_real) == 0;
}

int ohsh_list_dir_entries(const char *path, OhshListDirCallback callback, void *context) {
    DIR *directory = opendir(path);
    if (!directory) {
        set_error_from_errno();
        return -1;
    }

    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        char *full_path = join_path(path, entry->d_name);
        if (!full_path) {
            closedir(directory);
            snprintf(last_error, sizeof(last_error), "out of memory");
            return -1;
        }

        struct stat info;
        if (stat(full_path, &info) == 0) {
            OhshDirEntry ohsh_entry;
            ohsh_entry.name = entry->d_name;
            ohsh_entry.is_file = S_ISREG(info.st_mode);
            ohsh_entry.is_folder = S_ISDIR(info.st_mode);
            ohsh_entry.size = (unsigned long long)info.st_size;
            if (callback(&ohsh_entry, context) != 0) {
                free(full_path);
                closedir(directory);
                return -1;
            }
        }

        free(full_path);
    }

    closedir(directory);
    return 0;
}

static int print_entry(const OhshDirEntry *entry, void *context) {
    (void)context;
    printf("%s %s%s\n", entry->is_folder ? "📂" : "📄", entry->name, entry->is_folder ? "/" : "");
    return 0;
}

int ohsh_list_dir(const char *path) {
    return ohsh_list_dir_entries(path, print_entry, NULL);
}

char *ohsh_find_executable(const char *name) {
    if (!name || name[0] == '\0') return NULL;

    if (strchr(name, '/')) {
        return access(name, X_OK) == 0 ? dup_string(name) : NULL;
    }

    const char *path_env = getenv("PATH");
    if (!path_env) return NULL;

    char *path_copy = dup_string(path_env);
    if (!path_copy) return NULL;

    char *save = NULL;
    char *dir = strtok_r(path_copy, ":", &save);
    while (dir != NULL) {
        char *candidate = join_path(dir, name);
        if (candidate && access(candidate, X_OK) == 0) {
            free(path_copy);
            return candidate;
        }
        free(candidate);
        dir = strtok_r(NULL, ":", &save);
    }

    free(path_copy);
    return NULL;
}

int ohsh_create_file(const char *path) {
    int file = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (file < 0) {
        set_error_from_errno();
        return -1;
    }
    if (close(file) != 0) {
        set_error_from_errno();
        return -1;
    }
    return 0;
}

int ohsh_copy_file(const char *source, const char *destination) {
    int input = open(source, O_RDONLY);
    if (input < 0) {
        set_error_from_errno();
        return -1;
    }

    struct stat info;
    mode_t mode = stat(source, &info) == 0 ? info.st_mode & 0777 : 0644;
    int output = open(destination, O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (output < 0) {
        set_error_from_errno();
        close(input);
        return -1;
    }

    char buffer[16384];
    ssize_t bytes_read;
    while ((bytes_read = read(input, buffer, sizeof(buffer))) > 0) {
        ssize_t total = 0;
        while (total < bytes_read) {
            ssize_t written = write(output, buffer + total, (size_t)(bytes_read - total));
            if (written < 0) {
                set_error_from_errno();
                close(input);
                close(output);
                return -1;
            }
            total += written;
        }
    }

    int failed = bytes_read < 0;
    if (failed) set_error_from_errno();
    if (close(input) != 0) failed = 1;
    if (close(output) != 0) failed = 1;
    return failed ? -1 : 0;
}

int ohsh_read_file_to_stdout(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        set_error_from_errno();
        return -1;
    }

    char buffer[16384];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        fwrite(buffer, 1, bytes_read, stdout);
    }

    int failed = ferror(file) != 0;
    fclose(file);
    if (failed) {
        snprintf(last_error, sizeof(last_error), "read failed");
        return -1;
    }
    return 0;
}

int ohsh_delete_folder_recursive(const char *path) {
    DIR *directory = opendir(path);
    if (!directory) {
        set_error_from_errno();
        return -1;
    }

    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        char *child = join_path(path, entry->d_name);
        if (!child) {
            closedir(directory);
            snprintf(last_error, sizeof(last_error), "out of memory");
            return -1;
        }

        OhshPathType type = ohsh_path_type(child);
        int result = type == OHSH_PATH_FOLDER ? ohsh_delete_folder_recursive(child) : ohsh_delete_file(child);
        free(child);
        if (result != 0) {
            closedir(directory);
            return -1;
        }
    }

    closedir(directory);
    return ohsh_delete_folder(path);
}

int ohsh_make_executable(const char *path) {
    struct stat info;
    if (stat(path, &info) != 0) {
        set_error_from_errno();
        return -1;
    }
    if (chmod(path, info.st_mode | S_IXUSR | S_IXGRP | S_IXOTH) != 0) {
        set_error_from_errno();
        return -1;
    }
    return 0;
}

int ohsh_begin_redirect(const char *redirect_in, const char *redirect_out, int append, OhshRedirectState **state) {
    OhshRedirectState *redirect = calloc(1, sizeof(*redirect));
    if (!redirect) return -1;
    redirect->saved_stdin = -1;
    redirect->saved_stdout = -1;

    if (redirect_in) {
        int input = open(redirect_in, O_RDONLY);
        if (input < 0) {
            set_error_from_errno();
            free(redirect);
            return -1;
        }
        redirect->saved_stdin = dup(STDIN_FILENO);
        if (redirect->saved_stdin < 0 || dup2(input, STDIN_FILENO) < 0) {
            set_error_from_errno();
            close(input);
            free(redirect);
            return -1;
        }
        close(input);
    }

    if (redirect_out) {
        int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
        int output = open(redirect_out, flags, 0644);
        if (output < 0) {
            set_error_from_errno();
            ohsh_end_redirect(redirect);
            return -1;
        }
        redirect->saved_stdout = dup(STDOUT_FILENO);
        if (redirect->saved_stdout < 0 || dup2(output, STDOUT_FILENO) < 0) {
            set_error_from_errno();
            close(output);
            ohsh_end_redirect(redirect);
            return -1;
        }
        close(output);
    }

    *state = redirect;
    return 0;
}

void ohsh_end_redirect(OhshRedirectState *state) {
    if (!state) return;
    fflush(stdout);
    if (state->saved_stdin >= 0) {
        dup2(state->saved_stdin, STDIN_FILENO);
        close(state->saved_stdin);
    }
    if (state->saved_stdout >= 0) {
        dup2(state->saved_stdout, STDOUT_FILENO);
        close(state->saved_stdout);
    }
    free(state);
}

static int apply_child_redirection(const OhshProcessCommand *command) {
    if (command->redirect_in) {
        int input = open(command->redirect_in, O_RDONLY);
        if (input < 0) return -1;
        dup2(input, STDIN_FILENO);
        close(input);
    }
    if (command->redirect_out) {
        int flags = O_WRONLY | O_CREAT | (command->redirect_append ? O_APPEND : O_TRUNC);
        int output = open(command->redirect_out, flags, 0644);
        if (output < 0) return -1;
        dup2(output, STDOUT_FILENO);
        close(output);
    }
    return 0;
}

int ohsh_run_command(char *const argv[], const char *redirect_in, const char *redirect_out, int append) {
    OhshProcessCommand command = {argv, redirect_in, redirect_out, append};
    pid_t pid = fork();
    if (pid < 0) {
        set_error_from_errno();
        return -1;
    }
    if (pid == 0) {
        if (apply_child_redirection(&command) != 0) _exit(1);
        execvp(argv[0], argv);
        _exit(127);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        set_error_from_errno();
        return -1;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return -1;
}

int ohsh_run_pipeline(const OhshProcessCommand *commands, int count) {
    int pipes[count > 1 ? count - 1 : 1][2];
    for (int i = 0; i < count - 1; i++) {
        if (pipe(pipes[i]) != 0) {
            set_error_from_errno();
            return -1;
        }
    }

    for (int i = 0; i < count; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            set_error_from_errno();
            return -1;
        }
        if (pid == 0) {
            if (i > 0) dup2(pipes[i - 1][0], STDIN_FILENO);
            if (i < count - 1) dup2(pipes[i][1], STDOUT_FILENO);
            for (int j = 0; j < count - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            if (apply_child_redirection(&commands[i]) != 0) _exit(1);
            execvp(commands[i].argv[0], commands[i].argv);
            _exit(127);
        }
    }

    for (int i = 0; i < count - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    int result = 0;
    for (int i = 0; i < count; i++) {
        int status = 0;
        wait(&status);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) result = 1;
    }
    return result;
}

const char *ohsh_platform_error(void) {
    return last_error[0] ? last_error : "platform operation failed";
}

void ohsh_free(void *ptr) {
    free(ptr);
}
