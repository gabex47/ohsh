#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "executor.h"

#ifndef OHSH_SRC_DIR
#define OHSH_SRC_DIR "."
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef struct {
    int saved_stdin;
    int saved_stdout;
} RedirectionState;

typedef struct {
    char *name;
    int is_dir;
    off_t size;
} ListEntry;

static const char *OHSH_VERSION = "0.3";

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

static char *path_basename(const char *path) {
    const char *end = path + strlen(path);
    while (end > path && end[-1] == '/') end--;

    const char *start = end;
    while (start > path && start[-1] != '/') start--;

    if (end <= start) return dup_string(path);

    size_t length = (size_t)(end - start);
    char *base = malloc(length + 1);
    if (!base) return NULL;

    memcpy(base, start, length);
    base[length] = '\0';
    return base;
}

static void split_parent_base(const char *path, char **parent, char **base) {
    char *copy = dup_string(path);
    if (!copy) {
        *parent = NULL;
        *base = NULL;
        return;
    }

    size_t length = strlen(copy);
    while (length > 1 && copy[length - 1] == '/') {
        copy[length - 1] = '\0';
        length--;
    }

    char *slash = strrchr(copy, '/');
    if (!slash) {
        *parent = dup_string(".");
        *base = dup_string(copy);
    } else if (slash == copy) {
        *parent = dup_string("/");
        *base = dup_string(slash + 1);
    } else {
        *slash = '\0';
        *parent = dup_string(copy);
        *base = dup_string(slash + 1);
    }

    free(copy);
}

static char *expand_user_path(const char *path) {
    if (!path || path[0] == '\0') return dup_string(".");

    if (strcmp(path, "~") == 0) {
        const char *home = getenv("HOME");
        return dup_string(home ? home : ".");
    }

    if (path[0] == '~' && path[1] == '/') {
        const char *home = getenv("HOME");
        if (!home) return dup_string(path);
        return join_path(home, path + 2);
    }

    return dup_string(path);
}

static char *compact_path(const char *path) {
    const char *home = getenv("HOME");

    if (home && path && strcmp(path, home) == 0) {
        return dup_string("~");
    }

    if (home && path && strncmp(path, home, strlen(home)) == 0 && path[strlen(home)] == '/') {
        char *relative = join_path("~", path + strlen(home) + 1);
        return relative ? relative : dup_string(path);
    }

    return dup_string(path ? path : ".");
}

static char *current_compact_path(void) {
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) {
        return dup_string(".");
    }
    return compact_path(cwd);
}

static int path_is_directory(const char *path) {
    struct stat info;
    if (stat(path, &info) != 0) return 0;
    return S_ISDIR(info.st_mode);
}

static int names_match_extension(const char *name, const char *extension) {
    if (!extension || extension[0] == '\0') return 1;

    const char *dot = strrchr(name, '.');
    if (!dot || dot[1] == '\0') return 0;

    return strcasecmp(dot + 1, extension) == 0;
}

static int min_int(int a, int b, int c) {
    int min = a < b ? a : b;
    return min < c ? min : c;
}

static int edit_distance(const char *a, const char *b) {
    int a_len = (int)strlen(a);
    int b_len = (int)strlen(b);

    int *previous = malloc(sizeof(int) * (size_t)(b_len + 1));
    int *current = malloc(sizeof(int) * (size_t)(b_len + 1));
    if (!previous || !current) {
        free(previous);
        free(current);
        return 9999;
    }

    for (int j = 0; j <= b_len; j++) previous[j] = j;

    for (int i = 1; i <= a_len; i++) {
        current[0] = i;
        for (int j = 1; j <= b_len; j++) {
            int left = current[j - 1] + 1;
            int up = previous[j] + 1;
            int diagonal = previous[j - 1] +
                (tolower((unsigned char)a[i - 1]) == tolower((unsigned char)b[j - 1]) ? 0 : 1);
            current[j] = min_int(left, up, diagonal);
        }

        int *tmp = previous;
        previous = current;
        current = tmp;
    }

    int result = previous[b_len];
    free(previous);
    free(current);
    return result;
}

static char *find_similar_path(const char *missing_path) {
    char *parent = NULL;
    char *base = NULL;
    split_parent_base(missing_path, &parent, &base);
    if (!parent || !base || base[0] == '\0') {
        free(parent);
        free(base);
        return NULL;
    }

    char *expanded_parent = expand_user_path(parent);
    DIR *directory = opendir(expanded_parent);
    free(expanded_parent);

    if (!directory) {
        free(parent);
        free(base);
        return NULL;
    }

    char *best = NULL;
    int best_distance = 9999;

    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        int distance = edit_distance(base, entry->d_name);
        if (distance < best_distance) {
            free(best);
            best = dup_string(entry->d_name);
            best_distance = distance;
        }
    }

    closedir(directory);

    int base_len = (int)strlen(base);
    int threshold = base_len <= 4 ? 1 : (base_len <= 10 ? 2 : base_len / 3);
    if (best_distance > threshold) {
        free(best);
        free(parent);
        free(base);
        return NULL;
    }

    char *suggestion = strcmp(parent, ".") == 0 ? dup_string(best) : join_path(parent, best);
    free(best);
    free(parent);
    free(base);
    return suggestion;
}

static void print_missing_path(const char *path) {
    printf("🔎 I couldn't find \"%s\".\n", path);

    char *suggestion = find_similar_path(path);
    if (suggestion) {
        printf("   Did you mean \"%s\"?\n", suggestion);
        free(suggestion);
    }
}

static void print_command_suggestions(void) {
    printf("\nTry one of these:\n");
    printf("  goto Downloads\n");
    printf("  show files\n");
    printf("  make folder Projects\n");
    printf("  read notes.txt\n");
    printf("  copy notes.txt to Backup\n");
    printf("  delete temp.txt\n");
    printf("\nType \"examples\" for more ideas.\n");
}

static char *resolve_command(const char *name) {
    if (!name || name[0] == '\0') return NULL;

    if (strchr(name, '/')) {
        return access(name, X_OK) == 0 ? dup_string(name) : NULL;
    }

    char *path_env = getenv("PATH");
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

static int copy_file_contents(const char *source, const char *destination, mode_t mode) {
    int input = open(source, O_RDONLY);
    if (input < 0) return -1;

    int output = open(destination, O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (output < 0) {
        close(input);
        return -1;
    }

    char buffer[16384];
    ssize_t bytes_read;

    while ((bytes_read = read(input, buffer, sizeof(buffer))) > 0) {
        ssize_t written_total = 0;
        while (written_total < bytes_read) {
            ssize_t written = write(output, buffer + written_total, (size_t)(bytes_read - written_total));
            if (written < 0) {
                close(input);
                close(output);
                return -1;
            }
            written_total += written;
        }
    }

    int read_error = bytes_read < 0;
    int close_input_error = close(input) != 0;
    int close_output_error = close(output) != 0;

    return read_error || close_input_error || close_output_error ? -1 : 0;
}

static char *resolve_destination_path(const char *source, const char *destination) {
    if (path_is_directory(destination)) {
        char *base = path_basename(source);
        char *joined = join_path(destination, base);
        free(base);
        return joined;
    }

    return dup_string(destination);
}

static int remove_recursive(const char *path) {
    struct stat info;
    if (lstat(path, &info) != 0) return -1;

    if (!S_ISDIR(info.st_mode)) {
        return unlink(path);
    }

    DIR *directory = opendir(path);
    if (!directory) return -1;

    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        char *child = join_path(path, entry->d_name);
        if (!child || remove_recursive(child) != 0) {
            free(child);
            closedir(directory);
            return -1;
        }
        free(child);
    }

    closedir(directory);
    return rmdir(path);
}

static int is_dangerous_delete_target(const char *target) {
    if (!target || target[0] == '\0') return 1;
    if (strcmp(target, "/") == 0 || strcmp(target, ".") == 0 || strcmp(target, "..") == 0 || strcmp(target, "~") == 0) {
        return 1;
    }

    char resolved[PATH_MAX];
    char home_resolved[PATH_MAX];
    char *expanded = expand_user_path(target);

    int dangerous = 0;
    if (expanded && realpath(expanded, resolved)) {
        if (strcmp(resolved, "/") == 0) dangerous = 1;

        const char *home = getenv("HOME");
        if (home && realpath(home, home_resolved) && strcmp(resolved, home_resolved) == 0) {
            dangerous = 1;
        }
    }

    free(expanded);
    return dangerous;
}

static int compare_entries(const void *left, const void *right) {
    const ListEntry *a = left;
    const ListEntry *b = right;
    return strcasecmp(a->name, b->name);
}

static int add_list_entry(ListEntry **entries, int *count, int *capacity, const char *name, int is_dir, off_t size) {
    if (*count >= *capacity) {
        int next_capacity = *capacity == 0 ? 32 : *capacity * 2;
        ListEntry *next = realloc(*entries, sizeof(ListEntry) * (size_t)next_capacity);
        if (!next) return 0;
        *entries = next;
        *capacity = next_capacity;
    }

    size_t length = strlen(name) + (is_dir ? 2 : 1);
    (*entries)[*count].name = malloc(length);
    if (!(*entries)[*count].name) return 0;

    strcpy((*entries)[*count].name, name);
    if (is_dir) strcat((*entries)[*count].name, "/");
    (*entries)[*count].is_dir = is_dir;
    (*entries)[*count].size = size;
    (*count)++;
    return 1;
}

static void free_list_entries(ListEntry *entries, int count) {
    for (int i = 0; i < count; i++) {
        free(entries[i].name);
    }
    free(entries);
}

static int run_program(char *const argv[]) {
    pid_t pid = fork();
    if (pid < 0) {
        printf("I couldn't start \"%s\": %s\n", argv[0], strerror(errno));
        return 1;
    }

    if (pid == 0) {
        execvp(argv[0], argv);
        fprintf(stderr, "I couldn't start \"%s\": %s\n", argv[0], strerror(errno));
        _exit(127);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        printf("I lost track of \"%s\": %s\n", argv[0], strerror(errno));
        return 1;
    }

    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 1;
}

void init_shell_context(ShellContext *context) {
    context->count = 0;
    context->commands_since_tip = 0;
    context->next_tip = 0;
    context->tips_enabled = 1;
    for (int i = 0; i < OHSH_HISTORY_MAX; i++) {
        context->items[i] = NULL;
    }
}

void add_history(ShellContext *context, const char *line) {
    if (!context || !line || line[0] == '\0') return;

    if (context->count == OHSH_HISTORY_MAX) {
        free(context->items[0]);
        memmove(&context->items[0], &context->items[1], sizeof(char *) * (OHSH_HISTORY_MAX - 1));
        context->count--;
    }

    context->items[context->count++] = dup_string(line);
}

void free_shell_context(ShellContext *context) {
    if (!context) return;

    for (int i = 0; i < context->count; i++) {
        free(context->items[i]);
        context->items[i] = NULL;
    }
    context->count = 0;
}

void print_welcome(void) {
    char *where = current_compact_path();

    printf("\n");
    printf("🌊 OHSH v%s\n", OHSH_VERSION);
    printf("Human-first shell\n");
    printf("\n");
    printf("Type \"help\" to explore commands.\n");
    printf("Type \"examples\" to see what you can say.\n");
    printf("\n");
    printf("📁 %s\n", where ? where : ".");
    printf("\n");

    free(where);
}

void print_prompt(void) {
    char *where = current_compact_path();
    printf("📁 %s\n> ", where ? where : ".");
    fflush(stdout);
    free(where);
}

void maybe_print_tip(ShellContext *context, CommandKind kind) {
    static const char *const tips[] = {
        "💡 Tip: You can say \"go to Downloads\" instead of \"goto Downloads\".",
        "💡 Tip: Try \"make me a folder called Images\".",
        "💡 Tip: \"show files bigger than 10mb\" works too.",
        "💡 Tip: Type \"examples\" whenever you want ideas.",
        "💡 Tip: Use quotes for names with spaces, like \"project notes.txt\"."
    };

    if (!context || !context->tips_enabled) return;
    if (kind == COMMAND_HELP || kind == COMMAND_EXAMPLES || kind == COMMAND_UNKNOWN ||
        kind == COMMAND_EXTERNAL || kind == COMMAND_EXIT || kind == COMMAND_EMPTY) {
        return;
    }

    context->commands_since_tip++;
    if (context->commands_since_tip < 4) return;

    printf("\n%s\n\n", tips[context->next_tip]);
    context->next_tip = (context->next_tip + 1) % (int)(sizeof(tips) / sizeof(tips[0]));
    context->commands_since_tip = 0;
}

static void print_examples(void) {
    printf("\n");
    printf("✨ Things you can say\n");
    printf("--------------------\n");
    printf("\n");
    printf("📁 Navigation\n");
    printf("  goto Downloads\n");
    printf("  go to Projects\n");
    printf("  open Documents\n");
    printf("  take me to Desktop\n");
    printf("  go back\n");
    printf("  where am i\n");
    printf("\n");
    printf("📂 Looking around\n");
    printf("  show files\n");
    printf("  show folders\n");
    printf("  show everything\n");
    printf("  show txt files\n");
    printf("  show files bigger than 10mb\n");
    printf("\n");
    printf("🛠 Creating\n");
    printf("  make folder Games\n");
    printf("  make me a folder called Images\n");
    printf("  create file README.md\n");
    printf("  new file \"project notes.txt\"\n");
    printf("\n");
    printf("📦 Organizing\n");
    printf("  copy README.md to Backup\n");
    printf("  move logo.png into Assets\n");
    printf("  rename draft.txt to final.txt\n");
    printf("  delete temp.txt\n");
    printf("  delete every txt file\n");
    printf("\n");
    printf("💻 Shell\n");
    printf("  clear screen\n");
    printf("  please clear the screen\n");
    printf("  say hello\n");
    printf("  show history\n");
    printf("  color cyan\n");
    printf("\n");
}

static void print_help(void) {
    printf("\n");
    printf("🌊 OHSH help\n");
    printf("------------\n");
    printf("Say what you want to do. OHSH will try to understand the intent.\n");
    printf("\n");
    printf("📁 Navigation\n");
    printf("  goto Downloads          go to a folder\n");
    printf("  open Documents          open a folder in OHSH\n");
    printf("  take me to Desktop      natural navigation\n");
    printf("  go home / go back       jump home or up one folder\n");
    printf("  where am i              show your current location\n");
    printf("\n");
    printf("📂 Files\n");
    printf("  show files              list files here\n");
    printf("  show folders            list folders here\n");
    printf("  show everything         include hidden items\n");
    printf("  read notes.txt          print a file\n");
    printf("\n");
    printf("🛠 Create\n");
    printf("  make folder Games\n");
    printf("  make me a folder called Images\n");
    printf("  make file README.md\n");
    printf("\n");
    printf("📦 Organize\n");
    printf("  copy notes.txt to Backup\n");
    printf("  move logo.png into Assets\n");
    printf("  delete temp.txt\n");
    printf("  delete every txt file\n");
    printf("\n");
    printf("💻 Shell\n");
    printf("  clear screen / please clear the screen\n");
    printf("  show history\n");
    printf("  color cyan / color reset\n");
    printf("  examples\n");
    printf("  exit\n");
    printf("\n");
    printf("Tip: Traditional commands like git, grep, cat, and make still work.\n");
    printf("\n");
}

static int apply_redirections(const Command *command, RedirectionState *state) {
    state->saved_stdin = -1;
    state->saved_stdout = -1;

    if (command->redirect_in) {
        int input = open(command->redirect_in, O_RDONLY);
        if (input < 0) {
            print_missing_path(command->redirect_in);
            return -1;
        }

        state->saved_stdin = dup(STDIN_FILENO);
        if (state->saved_stdin < 0 || dup2(input, STDIN_FILENO) < 0) {
            printf("I couldn't read from \"%s\": %s\n", command->redirect_in, strerror(errno));
            close(input);
            return -1;
        }
        close(input);
    }

    if (command->redirect_out) {
        int flags = O_WRONLY | O_CREAT | (command->redirect_append ? O_APPEND : O_TRUNC);
        int output = open(command->redirect_out, flags, 0644);
        if (output < 0) {
            printf("I couldn't write to \"%s\": %s\n", command->redirect_out, strerror(errno));
            return -1;
        }

        state->saved_stdout = dup(STDOUT_FILENO);
        if (state->saved_stdout < 0 || dup2(output, STDOUT_FILENO) < 0) {
            printf("I couldn't write to \"%s\": %s\n", command->redirect_out, strerror(errno));
            close(output);
            return -1;
        }
        close(output);
    }

    return 0;
}

static void restore_redirections(RedirectionState *state) {
    if (state->saved_stdin >= 0) {
        dup2(state->saved_stdin, STDIN_FILENO);
        close(state->saved_stdin);
    }

    if (state->saved_stdout >= 0) {
        fflush(stdout);
        dup2(state->saved_stdout, STDOUT_FILENO);
        close(state->saved_stdout);
    }
}

static int apply_child_redirections(const Command *command) {
    if (command->redirect_in) {
        int input = open(command->redirect_in, O_RDONLY);
        if (input < 0) {
            fprintf(stderr, "I couldn't read from \"%s\": %s\n", command->redirect_in, strerror(errno));
            return -1;
        }
        dup2(input, STDIN_FILENO);
        close(input);
    }

    if (command->redirect_out) {
        int flags = O_WRONLY | O_CREAT | (command->redirect_append ? O_APPEND : O_TRUNC);
        int output = open(command->redirect_out, flags, 0644);
        if (output < 0) {
            fprintf(stderr, "I couldn't write to \"%s\": %s\n", command->redirect_out, strerror(errno));
            return -1;
        }
        dup2(output, STDOUT_FILENO);
        close(output);
    }

    return 0;
}

static int change_directory(const Command *command) {
    char *target = expand_user_path(command->target ? command->target : "~");
    if (!target) return 1;

    if (chdir(target) != 0) {
        if (errno == ENOENT) {
            print_missing_path(command->target ? command->target : target);
        } else {
            printf("I couldn't go to \"%s\": %s\n", command->target ? command->target : target, strerror(errno));
        }
        free(target);
        return 1;
    }

    char *where = current_compact_path();
    printf("📁 Now in %s\n", where ? where : command->target);
    free(where);
    free(target);
    return 0;
}

static int show_location(void) {
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) {
        printf("I couldn't figure out where we are: %s\n", strerror(errno));
        return 1;
    }

    char *where = compact_path(cwd);
    printf("📍 You're here\n\n%s\n", where ? where : cwd);
    free(where);
    return 0;
}

static int list_directory(const Command *command) {
    const char *raw_target = command->target ? command->target : ".";
    char *target = expand_user_path(raw_target);
    if (!target) return 1;

    DIR *directory = opendir(target);
    if (!directory) {
        if (errno == ENOENT) {
            print_missing_path(raw_target);
        } else {
            printf("I couldn't show \"%s\": %s\n", raw_target, strerror(errno));
        }
        free(target);
        return 1;
    }

    ListEntry *entries = NULL;
    int count = 0;
    int capacity = 0;

    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        if (!command->include_hidden && entry->d_name[0] == '.') continue;

        char *full_path = join_path(target, entry->d_name);
        if (!full_path) continue;

        struct stat info;
        if (lstat(full_path, &info) != 0) {
            free(full_path);
            continue;
        }

        int is_dir = S_ISDIR(info.st_mode);
        int is_file = S_ISREG(info.st_mode);

        if (command->list_mode == LIST_FILES && !is_file) {
            free(full_path);
            continue;
        }

        if (command->list_mode == LIST_FOLDERS && !is_dir) {
            free(full_path);
            continue;
        }

        if (command->filter_extension && (!is_file || !names_match_extension(entry->d_name, command->filter_extension))) {
            free(full_path);
            continue;
        }

        if (command->has_min_size && (!is_file || (size_t)info.st_size <= command->min_size_bytes)) {
            free(full_path);
            continue;
        }

        add_list_entry(&entries, &count, &capacity, entry->d_name, is_dir, info.st_size);
        free(full_path);
    }

    closedir(directory);
    free(target);

    if (count == 0) {
        printf("🌿 Nothing matched.\n");
        free_list_entries(entries, count);
        return 0;
    }

    qsort(entries, (size_t)count, sizeof(ListEntry), compare_entries);
    for (int i = 0; i < count; i++) {
        printf("%s %s\n", entries[i].is_dir ? "📂" : "📄", entries[i].name);
    }

    free_list_entries(entries, count);
    return 0;
}

static int make_folder(const Command *command) {
    char *target = expand_user_path(command->target);
    if (!target) return 1;

    if (mkdir(target, 0755) != 0) {
        if (errno == EEXIST && path_is_directory(target)) {
            printf("📂 That folder already exists: \"%s\"\n", command->target);
        } else {
            printf("I couldn't create folder \"%s\": %s\n", command->target, strerror(errno));
        }
        free(target);
        return 1;
    }

    printf("✅ Created folder \"%s\".\n", command->target);
    free(target);
    return 0;
}

static int make_file(const Command *command) {
    char *target = expand_user_path(command->target);
    if (!target) return 1;

    int file = open(target, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (file < 0) {
        if (errno == EEXIST) {
            printf("📄 That file already exists: \"%s\"\n", command->target);
        } else {
            printf("I couldn't create file \"%s\": %s\n", command->target, strerror(errno));
        }
        free(target);
        return 1;
    }

    close(file);
    printf("✅ Created file \"%s\".\n", command->target);
    free(target);
    return 0;
}

static int read_file_command(const Command *command) {
    char *target = expand_user_path(command->target);
    if (!target) return 1;

    struct stat info;
    if (stat(target, &info) != 0) {
        print_missing_path(command->target);
        free(target);
        return 1;
    }

    if (S_ISDIR(info.st_mode)) {
        printf("📂 \"%s\" is a folder.\n", command->target);
        printf("   Try: show files in %s\n", command->target);
        free(target);
        return 1;
    }

    FILE *file = fopen(target, "rb");
    if (!file) {
        printf("I couldn't read \"%s\": %s\n", command->target, strerror(errno));
        free(target);
        return 1;
    }

    char buffer[16384];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        fwrite(buffer, 1, bytes_read, stdout);
    }

    if (ferror(file)) {
        printf("\nI had trouble reading \"%s\".\n", command->target);
        fclose(file);
        free(target);
        return 1;
    }

    fclose(file);
    free(target);
    return 0;
}

static int copy_path_command(const Command *command) {
    char *source = expand_user_path(command->source);
    char *destination = expand_user_path(command->destination);
    if (!source || !destination) {
        free(source);
        free(destination);
        return 1;
    }

    struct stat info;
    if (stat(source, &info) != 0) {
        print_missing_path(command->source);
        free(source);
        free(destination);
        return 1;
    }

    if (S_ISDIR(info.st_mode)) {
        printf("📂 I can copy files right now, but folder copying is not enabled yet.\n");
        printf("   Try: copy notes.txt to Backup\n");
        free(source);
        free(destination);
        return 1;
    }

    char *final_destination = resolve_destination_path(source, destination);
    if (!final_destination) {
        free(source);
        free(destination);
        return 1;
    }

    if (copy_file_contents(source, final_destination, info.st_mode & 0777) != 0) {
        printf("I couldn't copy \"%s\" to \"%s\": %s\n", command->source, command->destination, strerror(errno));
        free(source);
        free(destination);
        free(final_destination);
        return 1;
    }

    printf("📄 Copied \"%s\" → \"%s\".\n", command->source, command->destination);
    free(source);
    free(destination);
    free(final_destination);
    return 0;
}

static int move_path_command(const Command *command) {
    char *source = expand_user_path(command->source);
    char *destination = expand_user_path(command->destination);
    if (!source || !destination) {
        free(source);
        free(destination);
        return 1;
    }

    struct stat info;
    if (stat(source, &info) != 0) {
        print_missing_path(command->source);
        free(source);
        free(destination);
        return 1;
    }

    char *final_destination = resolve_destination_path(source, destination);
    if (!final_destination) {
        free(source);
        free(destination);
        return 1;
    }

    if (rename(source, final_destination) != 0) {
        if (errno == EXDEV && S_ISREG(info.st_mode)) {
            if (copy_file_contents(source, final_destination, info.st_mode & 0777) == 0 && unlink(source) == 0) {
                printf("📦 Moved \"%s\" → \"%s\".\n", command->source, command->destination);
                free(source);
                free(destination);
                free(final_destination);
                return 0;
            }
        }

        printf("I couldn't move \"%s\" to \"%s\": %s\n", command->source, command->destination, strerror(errno));
        free(source);
        free(destination);
        free(final_destination);
        return 1;
    }

    printf("📦 Moved \"%s\" → \"%s\".\n", command->source, command->destination);
    free(source);
    free(destination);
    free(final_destination);
    return 0;
}

static int delete_bulk_files(const Command *command) {
    if (!command->filter_extension) {
        printf("Delete what kind of file?\n\nExample:\n  delete every txt file\n");
        return 1;
    }

    DIR *directory = opendir(".");
    if (!directory) {
        printf("I couldn't look through this folder: %s\n", strerror(errno));
        return 1;
    }

    int deleted = 0;
    int failed = 0;

    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        if (!names_match_extension(entry->d_name, command->filter_extension)) continue;

        struct stat info;
        if (lstat(entry->d_name, &info) != 0 || !S_ISREG(info.st_mode)) continue;

        if (unlink(entry->d_name) == 0) {
            deleted++;
        } else {
            printf("I couldn't delete \"%s\": %s\n", entry->d_name, strerror(errno));
            failed++;
        }
    }

    closedir(directory);

    if (deleted == 0 && failed == 0) {
        printf("🌿 I didn't find any .%s files to delete.\n", command->filter_extension);
    } else if (failed == 0) {
        printf("🗑 Deleted %d .%s file%s.\n", deleted, command->filter_extension, deleted == 1 ? "" : "s");
    }

    return failed == 0 ? 0 : 1;
}

static int delete_path_command(const Command *command) {
    if (command->bulk) return delete_bulk_files(command);

    if (is_dangerous_delete_target(command->target)) {
        printf("🛡 I won't delete \"%s\" because that target is too broad or too important.\n", command->target);
        return 1;
    }

    char *target = expand_user_path(command->target);
    if (!target) return 1;

    struct stat info;
    if (lstat(target, &info) != 0) {
        print_missing_path(command->target);
        free(target);
        return 1;
    }

    if (S_ISDIR(info.st_mode)) {
        if (command->recursive) {
            if (remove_recursive(target) != 0) {
                printf("I couldn't delete folder \"%s\": %s\n", command->target, strerror(errno));
                free(target);
                return 1;
            }

            printf("🗑 Deleted folder \"%s\" permanently.\n", command->target);
            free(target);
            return 0;
        }

        if (rmdir(target) != 0) {
            if (errno == ENOTEMPTY) {
                printf("📂 \"%s\" is not empty.\n", command->target);
                printf("   If you really want to delete it and everything inside, type:\n");
                printf("   delete %s permanently\n", command->target);
            } else {
                printf("I couldn't delete folder \"%s\": %s\n", command->target, strerror(errno));
            }
            free(target);
            return 1;
        }

        printf("🗑 Deleted folder \"%s\".\n", command->target);
        free(target);
        return 0;
    }

    if (unlink(target) != 0) {
        printf("I couldn't delete \"%s\": %s\n", command->target, strerror(errno));
        free(target);
        return 1;
    }

    printf("🗑 Deleted \"%s\".\n", command->target);
    free(target);
    return 0;
}

static int show_history(const ShellContext *context) {
    if (!context || context->count == 0) {
        printf("🕘 No commands yet.\n");
        return 0;
    }

    printf("🕘 Recent commands\n");
    for (int i = 0; i < context->count; i++) {
        printf("%3d  %s\n", i + 1, context->items[i]);
    }

    return 0;
}

static int set_color(const Command *command) {
    if (!command->color) {
        printf("What color should I use?\n\nExample:\n  color cyan\n");
        return 1;
    }

    if (strcasecmp(command->color, "red") == 0) printf("\033[31m");
    else if (strcasecmp(command->color, "green") == 0) printf("\033[32m");
    else if (strcasecmp(command->color, "yellow") == 0) printf("\033[33m");
    else if (strcasecmp(command->color, "blue") == 0) printf("\033[34m");
    else if (strcasecmp(command->color, "magenta") == 0) printf("\033[35m");
    else if (strcasecmp(command->color, "cyan") == 0) printf("\033[36m");
    else if (strcasecmp(command->color, "white") == 0) printf("\033[37m");
    else if (strcasecmp(command->color, "reset") == 0 || strcasecmp(command->color, "normal") == 0) printf("\033[0m");
    else {
        printf("I don't know the color \"%s\".\n", command->color);
        printf("Try: red, green, blue, yellow, cyan, magenta, white, or reset.\n");
        return 1;
    }

    fflush(stdout);
    printf("🎨 Color set to %s.\n", command->color);
    return 0;
}

static int update_ohsh(void) {
    char *makefile = join_path(OHSH_SRC_DIR, "Makefile");
    if (!makefile || access(makefile, R_OK) != 0) {
        printf("I can't rebuild from this installation because the source tree is not available.\n");
        printf("If you installed OHSH with Homebrew, update it with:\n");
        printf("  brew upgrade ohsh\n");
        printf("or, for the latest main branch build:\n");
        printf("  brew reinstall --HEAD gabex47/tap/ohsh\n");
        free(makefile);
        return 1;
    }
    free(makefile);

    printf("🛠 Rebuilding OHSH...\n");
    fflush(stdout);

    char *const build_args[] = {"make", "-C", OHSH_SRC_DIR, "build", NULL};
    int build_status = run_program(build_args);
    if (build_status != 0) {
        printf("The rebuild failed. Check the compiler output above.\n");
        return build_status;
    }

    char *source = join_path(OHSH_SRC_DIR, "ohsh");
    const char *destination = "/usr/local/bin/ohsh";

    if (!source) return 1;

    if (copy_file_contents(source, destination, 0755) != 0) {
        printf("OHSH was rebuilt, but I couldn't install it to \"%s\": %s\n", destination, strerror(errno));
        printf("You may need to install it with elevated permissions.\n");
        free(source);
        return 1;
    }

    chmod(destination, 0755);
    printf("✅ OHSH is up to date.\n");
    free(source);
    return 0;
}

static int run_external_parent(const Command *command) {
    char *path = resolve_command(command->name);
    if (!path) {
        printf("I don't recognize \"%s\" yet.\n", command->name);
        print_command_suggestions();
        return 127;
    }

    pid_t pid = fork();
    if (pid < 0) {
        printf("I couldn't start \"%s\": %s\n", command->name, strerror(errno));
        free(path);
        return 1;
    }

    if (pid == 0) {
        execv(path, command->args);
        fprintf(stderr, "I couldn't run \"%s\": %s\n", command->name, strerror(errno));
        _exit(127);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    free(path);

    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 1;
}

static void run_external_child(const Command *command) {
    char *path = resolve_command(command->name);
    if (!path) {
        fprintf(stderr, "I don't recognize \"%s\" yet.\n", command->name);
        _exit(127);
    }

    execv(path, command->args);
    fprintf(stderr, "I couldn't run \"%s\": %s\n", command->name, strerror(errno));
    free(path);
    _exit(127);
}

static int run_command_action(const Command *command, ShellContext *context, int in_child) {
    (void)in_child;

    switch (command->kind) {
        case COMMAND_EMPTY:
            return 0;
        case COMMAND_UNKNOWN:
            printf("%s\n", command->error ? command->error : "I don't understand that command.");
            if (!command->error || !strstr(command->error, "Example:")) {
                print_command_suggestions();
            }
            return 1;
        case COMMAND_HELP:
            print_help();
            return 0;
        case COMMAND_EXAMPLES:
            print_examples();
            return 0;
        case COMMAND_CHANGE_DIR:
            return change_directory(command);
        case COMMAND_PRINT_TEXT:
            printf("%s\n", command->text ? command->text : "");
            return 0;
        case COMMAND_WHERE_AM_I:
            return show_location();
        case COMMAND_LIST:
            return list_directory(command);
        case COMMAND_MAKE_FOLDER:
            return make_folder(command);
        case COMMAND_MAKE_FILE:
            return make_file(command);
        case COMMAND_DELETE_PATH:
            return delete_path_command(command);
        case COMMAND_COPY_PATH:
            return copy_path_command(command);
        case COMMAND_MOVE_PATH:
            return move_path_command(command);
        case COMMAND_READ_FILE:
            return read_file_command(command);
        case COMMAND_CLEAR_SCREEN:
            printf("\033[2J\033[H");
            fflush(stdout);
            return 0;
        case COMMAND_SHOW_HISTORY:
            return show_history(context);
        case COMMAND_COLOR:
            return set_color(command);
        case COMMAND_UPDATE:
            return update_ohsh();
        case COMMAND_EXTERNAL:
            return run_external_parent(command);
        case COMMAND_EXIT:
            return 0;
        default:
            printf("I don't recognize that yet.\n");
            print_command_suggestions();
            return 1;
    }
}

static int run_single_command(const Command *command, ShellContext *context) {
    if (command->kind == COMMAND_EXTERNAL) {
        return run_external_parent(command);
    }

    return run_command_action(command, context, 0);
}

static void run_pipeline_child(const Command *command, ShellContext *context) {
    if (apply_child_redirections(command) != 0) {
        _exit(1);
    }

    if (command->kind == COMMAND_EXTERNAL) {
        run_external_child(command);
    }

    int status = run_command_action(command, context, 1);
    fflush(NULL);
    _exit(status);
}

static int run_pipeline(Pipeline pipeline, ShellContext *context) {
    int count = pipeline.command_count;
    int pipes[MAX_COMMANDS - 1][2];

    for (int i = 0; i < count - 1; i++) {
        if (pipe(pipes[i]) != 0) {
            printf("I couldn't connect those commands: %s\n", strerror(errno));
            return 1;
        }
    }

    for (int i = 0; i < count; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            printf("I couldn't start part of the pipeline: %s\n", strerror(errno));
            return 1;
        }

        if (pid == 0) {
            if (i > 0) {
                dup2(pipes[i - 1][0], STDIN_FILENO);
            }

            if (i < count - 1) {
                dup2(pipes[i][1], STDOUT_FILENO);
            }

            for (int j = 0; j < count - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            run_pipeline_child(&pipeline.commands[i], context);
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

ExecutionResult execute(Pipeline pipeline, ShellContext *context) {
    if (pipeline.command_count == 0) return EXECUTION_CONTINUE;

    if (pipeline.command_count > 1) {
        run_pipeline(pipeline, context);
        return EXECUTION_CONTINUE;
    }

    Command *command = &pipeline.commands[0];

    if (command->kind == COMMAND_EXIT) {
        printf("bye\n");
        return EXECUTION_EXIT;
    }

    RedirectionState redirection;
    if (apply_redirections(command, &redirection) != 0) {
        restore_redirections(&redirection);
        return EXECUTION_CONTINUE;
    }

    run_single_command(command, context);
    restore_redirections(&redirection);
    return EXECUTION_CONTINUE;
}
