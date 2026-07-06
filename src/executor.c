#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "executor.h"
#include "platform/platform.h"

#ifndef OHSH_SRC_DIR
#define OHSH_SRC_DIR "."
#endif

typedef struct {
    char *name;
    int is_folder;
    unsigned long long size;
} ListEntry;

typedef struct {
    ListEntry *entries;
    int count;
    int capacity;
    const Command *command;
} ListContext;

typedef struct {
    const Command *command;
    int deleted;
    int failed;
} DeleteBulkContext;

typedef struct {
    const char *needle;
    char *best;
    int best_distance;
} SuggestionContext;

static const char *OHSH_VERSION = "0.3";

static char *dup_string(const char *value) {
    size_t length = strlen(value) + 1;
    char *copy = malloc(length);
    if (!copy) return NULL;
    memcpy(copy, value, length);
    return copy;
}

static int equals_ignore_case(const char *left, const char *right) {
    if (!left || !right) return 0;
    while (*left && *right) {
        if (tolower((unsigned char)*left) != tolower((unsigned char)*right)) return 0;
        left++;
        right++;
    }
    return *left == '\0' && *right == '\0';
}

static int compare_ignore_case(const char *left, const char *right) {
    while (*left && *right) {
        int a = tolower((unsigned char)*left);
        int b = tolower((unsigned char)*right);
        if (a != b) return a - b;
        left++;
        right++;
    }
    return (unsigned char)*left - (unsigned char)*right;
}

static char *trim_in_place(char *value) {
    while (*value && isspace((unsigned char)*value)) value++;
    char *end = value + strlen(value);
    while (end > value && isspace((unsigned char)end[-1])) end--;
    *end = '\0';
    if ((value[0] == '"' && end > value + 1 && end[-1] == '"') ||
        (value[0] == '\'' && end > value + 1 && end[-1] == '\'')) {
        value++;
        end[-1] = '\0';
    }
    return value;
}

static char *join_path(const char *left, const char *right) {
    if (!right) return NULL;
    if (right[0] == '/' || (strlen(right) > 2 && right[1] == ':')) return dup_string(right);
    if (!left || left[0] == '\0' || strcmp(left, ".") == 0) return dup_string(right);

    size_t left_len = strlen(left);
    size_t right_len = strlen(right);
    int needs_slash = left[left_len - 1] != '/' && left[left_len - 1] != '\\';
    char *joined = malloc(left_len + right_len + (needs_slash ? 2 : 1));
    if (!joined) return NULL;

    strcpy(joined, left);
    if (needs_slash) strcat(joined, "/");
    strcat(joined, right);
    return joined;
}

static char *path_basename(const char *path) {
    const char *end = path + strlen(path);
    while (end > path && (end[-1] == '/' || end[-1] == '\\')) end--;

    const char *start = end;
    while (start > path && start[-1] != '/' && start[-1] != '\\') start--;

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
    while (length > 1 && (copy[length - 1] == '/' || copy[length - 1] == '\\')) {
        copy[length - 1] = '\0';
        length--;
    }

    char *slash = strrchr(copy, '/');
    char *backslash = strrchr(copy, '\\');
    char *separator = slash > backslash ? slash : backslash;

    if (!separator) {
        *parent = dup_string(".");
        *base = dup_string(copy);
    } else if (separator == copy) {
        *parent = dup_string("/");
        *base = dup_string(separator + 1);
    } else {
        *separator = '\0';
        *parent = dup_string(copy);
        *base = dup_string(separator + 1);
    }

    free(copy);
}

static char *expand_user_path(const char *path) {
    if (!path || path[0] == '\0') return dup_string(".");

    if (strcmp(path, "~") == 0) {
        char *home = ohsh_get_home();
        return home ? home : dup_string(".");
    }

    if (path[0] == '~' && (path[1] == '/' || path[1] == '\\')) {
        char *home = ohsh_get_home();
        if (!home) return dup_string(path);
        char *expanded = join_path(home, path + 2);
        ohsh_free(home);
        return expanded;
    }

    return dup_string(path);
}

static char *compact_path(const char *path) {
    char *home = ohsh_get_home();
    if (!home) return dup_string(path ? path : ".");

    char *compact = NULL;
    size_t home_len = strlen(home);
    if (path && strcmp(path, home) == 0) {
        compact = dup_string("~");
    } else if (path && strncmp(path, home, home_len) == 0 &&
               (path[home_len] == '/' || path[home_len] == '\\')) {
        compact = join_path("~", path + home_len + 1);
    } else {
        compact = dup_string(path ? path : ".");
    }

    ohsh_free(home);
    return compact;
}

static char *current_compact_path(void) {
    char *cwd = ohsh_get_cwd();
    if (!cwd) return dup_string(".");
    char *compact = compact_path(cwd);
    ohsh_free(cwd);
    return compact;
}

static int names_match_extension(const char *name, const char *extension) {
    if (!extension || extension[0] == '\0') return 1;

    const char *dot = strrchr(name, '.');
    if (!dot || dot[1] == '\0') return 0;
    return equals_ignore_case(dot + 1, extension);
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

static int suggestion_callback(const OhshDirEntry *entry, void *context) {
    SuggestionContext *suggestion = context;
    int distance = edit_distance(suggestion->needle, entry->name);
    if (distance < suggestion->best_distance) {
        free(suggestion->best);
        suggestion->best = dup_string(entry->name);
        suggestion->best_distance = distance;
    }
    return 0;
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
    SuggestionContext context = {base, NULL, 9999};
    if (expanded_parent) {
        ohsh_list_dir_entries(expanded_parent, suggestion_callback, &context);
    }
    free(expanded_parent);

    int base_len = (int)strlen(base);
    int threshold = base_len <= 4 ? 1 : (base_len <= 10 ? 2 : base_len / 3);
    if (context.best_distance > threshold) {
        free(context.best);
        free(parent);
        free(base);
        return NULL;
    }

    char *result = strcmp(parent, ".") == 0 ? dup_string(context.best) : join_path(parent, context.best);
    free(context.best);
    free(parent);
    free(base);
    return result;
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

static int compare_entries(const void *left, const void *right) {
    const ListEntry *a = left;
    const ListEntry *b = right;
    return compare_ignore_case(a->name, b->name);
}

static int add_list_entry(ListEntry **entries, int *count, int *capacity, const char *name, int is_folder, unsigned long long size) {
    if (*count >= *capacity) {
        int next_capacity = *capacity == 0 ? 32 : *capacity * 2;
        ListEntry *next = realloc(*entries, sizeof(ListEntry) * (size_t)next_capacity);
        if (!next) return 0;
        *entries = next;
        *capacity = next_capacity;
    }

    (*entries)[*count].name = dup_string(name);
    if (!(*entries)[*count].name) return 0;
    (*entries)[*count].is_folder = is_folder;
    (*entries)[*count].size = size;
    (*count)++;
    return 1;
}

static void free_list_entries(ListEntry *entries, int count) {
    for (int i = 0; i < count; i++) free(entries[i].name);
    free(entries);
}

void init_shell_context(ShellContext *context) {
    context->count = 0;
    context->commands_since_tip = 0;
    context->next_tip = 0;
    context->tips_enabled = 1;
    context->confirm_destructive = 1;
    context->color_enabled = 1;
    context->non_interactive = 0;
    context->assume_yes = 0;
    context->debug_enabled = 0;
    context->last_status = 0;
    context->fallback_shell = NULL;
    context->alias_count = 0;
    for (int i = 0; i < OHSH_HISTORY_MAX; i++) context->items[i] = NULL;
    for (int i = 0; i < OHSH_ALIAS_MAX; i++) {
        context->aliases[i].phrase = NULL;
        context->aliases[i].replacement = NULL;
    }
}

static int parse_bool_value(const char *value, int fallback) {
    if (!value) return fallback;
    if (equals_ignore_case(value, "true") || equals_ignore_case(value, "yes") ||
        equals_ignore_case(value, "on") || strcmp(value, "1") == 0) {
        return 1;
    }
    if (equals_ignore_case(value, "false") || equals_ignore_case(value, "no") ||
        equals_ignore_case(value, "off") || strcmp(value, "0") == 0) {
        return 0;
    }
    return fallback;
}

static void add_alias(ShellContext *context, const char *phrase, const char *replacement) {
    if (!context || !phrase || !replacement || phrase[0] == '\0' || replacement[0] == '\0') return;
    if (context->alias_count >= OHSH_ALIAS_MAX) return;

    context->aliases[context->alias_count].phrase = dup_string(phrase);
    context->aliases[context->alias_count].replacement = dup_string(replacement);
    if (!context->aliases[context->alias_count].phrase || !context->aliases[context->alias_count].replacement) {
        free(context->aliases[context->alias_count].phrase);
        free(context->aliases[context->alias_count].replacement);
        context->aliases[context->alias_count].phrase = NULL;
        context->aliases[context->alias_count].replacement = NULL;
        return;
    }
    context->alias_count++;
}

static void load_config_file(ShellContext *context, const char *path) {
    FILE *file = fopen(path, "r");
    if (!file) return;

    char line[2048];
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\r\n")] = '\0';
        char *text = trim_in_place(line);
        if (text[0] == '\0' || text[0] == '#') continue;

        if (strncmp(text, "alias ", 6) == 0) {
            char *definition = trim_in_place(text + 6);
            char *equals = strchr(definition, '=');
            if (!equals) continue;
            *equals = '\0';
            char *phrase = trim_in_place(definition);
            char *replacement = trim_in_place(equals + 1);
            add_alias(context, phrase, replacement);
            continue;
        }

        char *equals = strchr(text, '=');
        if (!equals) continue;
        *equals = '\0';
        char *key = trim_in_place(text);
        char *value = trim_in_place(equals + 1);

        if (equals_ignore_case(key, "confirm") || equals_ignore_case(key, "confirm_destructive")) {
            context->confirm_destructive = parse_bool_value(value, context->confirm_destructive);
        } else if (equals_ignore_case(key, "tips")) {
            context->tips_enabled = parse_bool_value(value, context->tips_enabled);
        } else if (equals_ignore_case(key, "color") || equals_ignore_case(key, "colors")) {
            context->color_enabled = parse_bool_value(value, context->color_enabled);
        } else if (equals_ignore_case(key, "fallback_shell")) {
            free(context->fallback_shell);
            context->fallback_shell = dup_string(value);
        }
    }

    fclose(file);
}

void load_shell_config(ShellContext *context) {
    char *home = ohsh_get_home();
    if (home) {
        char *home_config = join_path(home, ".ohshrc");
        if (home_config) load_config_file(context, home_config);
        free(home_config);
        ohsh_free(home);
    }
    load_config_file(context, ".ohshrc");
}

static int alias_matches_prefix(const char *line, const char *phrase) {
    size_t length = strlen(phrase);
    if (strncasecmp(line, phrase, length) != 0) return 0;
    return line[length] == '\0' || isspace((unsigned char)line[length]);
}

char *expand_alias_line(const ShellContext *context, const char *line) {
    if (!context || !line) return NULL;
    for (int i = 0; i < context->alias_count; i++) {
        const char *phrase = context->aliases[i].phrase;
        const char *replacement = context->aliases[i].replacement;
        if (!phrase || !replacement || !alias_matches_prefix(line, phrase)) continue;

        const char *rest = line + strlen(phrase);
        while (*rest && isspace((unsigned char)*rest)) rest++;
        size_t length = strlen(replacement) + (*rest ? 1 + strlen(rest) : 0) + 1;
        char *expanded = malloc(length);
        if (!expanded) return NULL;
        if (*rest) snprintf(expanded, length, "%s %s", replacement, rest);
        else snprintf(expanded, length, "%s", replacement);
        return expanded;
    }
    return NULL;
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
    for (int i = 0; i < context->alias_count; i++) {
        free(context->aliases[i].phrase);
        free(context->aliases[i].replacement);
        context->aliases[i].phrase = NULL;
        context->aliases[i].replacement = NULL;
    }
    free(context->fallback_shell);
    context->fallback_shell = NULL;
    context->alias_count = 0;
    context->count = 0;
}

void print_welcome(void) {
    char *where = current_compact_path();
    printf("\n");
    printf("🌊 OHSH v%s\n", OHSH_VERSION);
    printf("Human-first shell\n\n");
    printf("Type \"help\" to explore commands.\n");
    printf("Type \"examples\" to see what you can say.\n\n");
    printf("📁 %s\n\n", where ? where : ".");
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
    printf("--------------------\n\n");
    printf("📁 Navigation\n");
    printf("  goto Downloads\n  go to Projects\n  open Documents\n  take me to Desktop\n  go back\n  where am i\n\n");
    printf("📂 Looking around\n");
    printf("  show files\n  show folders\n  show everything\n  show txt files\n  show files bigger than 10mb\n\n");
    printf("🛠 Creating\n");
    printf("  make folder Games\n  make me a folder called Images\n  create file README.md\n  new file \"project notes.txt\"\n\n");
    printf("📦 Organizing\n");
    printf("  copy README.md to Backup\n  move logo.png into Assets\n  rename draft.txt to final.txt\n  delete temp.txt\n  delete every txt file\n\n");
    printf("💻 Shell\n");
    printf("  clear screen\n  please clear the screen\n  say hello\n  show history\n  color cyan\n\n");
}

static void print_help(void) {
    printf("\n");
    printf("🌊 OHSH help\n");
    printf("------------\n");
    printf("Say what you want to do. OHSH will try to understand the intent.\n\n");
    printf("📁 Navigation\n");
    printf("  goto Downloads          go to a folder\n");
    printf("  open Documents          open a folder in OHSH\n");
    printf("  take me to Desktop      natural navigation\n");
    printf("  go home / go back       jump home or up one folder\n");
    printf("  where am i              show your current location\n\n");
    printf("📂 Files\n");
    printf("  show files              list files here\n");
    printf("  show folders            list folders here\n");
    printf("  show everything         include hidden items\n");
    printf("  read notes.txt          print a file\n\n");
    printf("🛠 Create\n");
    printf("  make folder Games\n");
    printf("  make me a folder called Images\n");
    printf("  make file README.md\n\n");
    printf("📦 Organize\n");
    printf("  copy notes.txt to Backup\n");
    printf("  move logo.png into Assets\n");
    printf("  delete temp.txt\n");
    printf("  delete every txt file\n\n");
    printf("💻 Shell\n");
    printf("  clear screen / please clear the screen\n");
    printf("  show history\n");
    printf("  color cyan / color reset\n");
    printf("  examples\n");
    printf("  exit\n\n");
    printf("Tip: Traditional commands like git, grep, cat, and make still work.\n\n");
}

static int change_directory(const Command *command) {
    char *target = expand_user_path(command->target ? command->target : "~");
    if (!target) return 1;

    if (ohsh_cd(target) != 0) {
        if (ohsh_path_type(target) == OHSH_PATH_MISSING) print_missing_path(command->target ? command->target : target);
        else printf("I couldn't go to \"%s\": %s\n", command->target ? command->target : target, ohsh_platform_error());
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
    char *cwd = ohsh_get_cwd();
    if (!cwd) {
        printf("I couldn't figure out where we are: %s\n", ohsh_platform_error());
        return 1;
    }
    char *where = compact_path(cwd);
    printf("📍 You're here\n\n%s\n", where ? where : cwd);
    free(where);
    ohsh_free(cwd);
    return 0;
}

static int list_callback(const OhshDirEntry *entry, void *context) {
    ListContext *list = context;
    const Command *command = list->command;

    if (!command->include_hidden && entry->name[0] == '.') return 0;
    if (command->list_mode == LIST_FILES && !entry->is_file) return 0;
    if (command->list_mode == LIST_FOLDERS && !entry->is_folder) return 0;
    if (command->filter_extension && (!entry->is_file || !names_match_extension(entry->name, command->filter_extension))) return 0;
    if (command->has_min_size && (!entry->is_file || entry->size <= command->min_size_bytes)) return 0;

    return add_list_entry(&list->entries, &list->count, &list->capacity, entry->name, entry->is_folder, entry->size) ? 0 : -1;
}

static int list_directory(const Command *command) {
    const char *raw_target = command->target ? command->target : ".";
    char *target = expand_user_path(raw_target);
    if (!target) return 1;

    ListContext context;
    context.entries = NULL;
    context.count = 0;
    context.capacity = 0;
    context.command = command;

    if (ohsh_list_dir_entries(target, list_callback, &context) != 0) {
        if (ohsh_path_type(target) == OHSH_PATH_MISSING) print_missing_path(raw_target);
        else printf("I couldn't show \"%s\": %s\n", raw_target, ohsh_platform_error());
        free(target);
        free_list_entries(context.entries, context.count);
        return 1;
    }

    free(target);
    if (context.count == 0) {
        printf("🌿 Nothing matched.\n");
        free_list_entries(context.entries, context.count);
        return 0;
    }

    qsort(context.entries, (size_t)context.count, sizeof(ListEntry), compare_entries);
    for (int i = 0; i < context.count; i++) {
        printf("%s %s%s\n", context.entries[i].is_folder ? "📂" : "📄",
               context.entries[i].name, context.entries[i].is_folder ? "/" : "");
    }
    free_list_entries(context.entries, context.count);
    return 0;
}

static int make_folder(const Command *command) {
    char *target = expand_user_path(command->target);
    if (!target) return 1;

    if (ohsh_mkdir(target) != 0) {
        if (ohsh_path_type(target) == OHSH_PATH_FOLDER) printf("📂 That folder already exists: \"%s\"\n", command->target);
        else printf("I couldn't create folder \"%s\": %s\n", command->target, ohsh_platform_error());
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

    if (ohsh_create_file(target) != 0) {
        if (ohsh_path_type(target) != OHSH_PATH_MISSING) printf("📄 That file already exists: \"%s\"\n", command->target);
        else printf("I couldn't create file \"%s\": %s\n", command->target, ohsh_platform_error());
        free(target);
        return 1;
    }

    printf("✅ Created file \"%s\".\n", command->target);
    free(target);
    return 0;
}

static int read_file_command(const Command *command) {
    char *target = expand_user_path(command->target);
    if (!target) return 1;

    OhshPathType type = ohsh_path_type(target);
    if (type == OHSH_PATH_MISSING) {
        print_missing_path(command->target);
        free(target);
        return 1;
    }
    if (type == OHSH_PATH_FOLDER) {
        printf("📂 \"%s\" is a folder.\n", command->target);
        printf("   Try: show files in %s\n", command->target);
        free(target);
        return 1;
    }

    if (ohsh_read_file_to_stdout(target) != 0) {
        printf("I couldn't read \"%s\": %s\n", command->target, ohsh_platform_error());
        free(target);
        return 1;
    }

    free(target);
    return 0;
}

static char *resolve_destination_path(const char *source, const char *destination) {
    if (ohsh_path_type(destination) == OHSH_PATH_FOLDER) {
        char *base = path_basename(source);
        char *joined = join_path(destination, base);
        free(base);
        return joined;
    }
    return dup_string(destination);
}

static int confirm_action(ShellContext *context, const Command *command, const char *message) {
    if (!context || !context->confirm_destructive || context->assume_yes || command->force) return 1;
    if (context->non_interactive) {
        printf("🛡 %s\n", message);
        printf("   Script mode will not continue without --yes, -y, or --force.\n");
        return 0;
    }

    char answer[32];
    printf("🛡 %s\n", message);
    printf("   Type yes to continue: ");
    fflush(stdout);
    if (!fgets(answer, sizeof(answer), stdin)) return 0;
    answer[strcspn(answer, "\r\n")] = '\0';
    return equals_ignore_case(answer, "yes") || equals_ignore_case(answer, "y");
}

static int copy_path_command(const Command *command, ShellContext *context) {
    char *source = expand_user_path(command->source);
    char *destination = expand_user_path(command->destination);
    if (!source || !destination) {
        free(source);
        free(destination);
        return 1;
    }

    OhshPathType source_type = ohsh_path_type(source);
    if (source_type == OHSH_PATH_MISSING) {
        print_missing_path(command->source);
        free(source);
        free(destination);
        return 1;
    }
    if (source_type == OHSH_PATH_FOLDER) {
        printf("📂 I can copy files right now, but folder copying is not enabled yet.\n");
        printf("   Try: copy notes.txt to Backup\n");
        free(source);
        free(destination);
        return 1;
    }

    char *final_destination = resolve_destination_path(source, destination);
    if (final_destination && ohsh_path_type(final_destination) != OHSH_PATH_MISSING) {
        char message[1024];
        snprintf(message, sizeof(message), "Copying will overwrite \"%s\".", command->destination);
        if (!confirm_action(context, command, message)) {
            free(source);
            free(destination);
            free(final_destination);
            return 1;
        }
    }

    if (!final_destination || ohsh_copy_file(source, final_destination) != 0) {
        printf("I couldn't copy \"%s\" to \"%s\": %s\n", command->source, command->destination, ohsh_platform_error());
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

static int move_path_command(const Command *command, ShellContext *context) {
    char *source = expand_user_path(command->source);
    char *destination = expand_user_path(command->destination);
    if (!source || !destination) {
        free(source);
        free(destination);
        return 1;
    }

    OhshPathType source_type = ohsh_path_type(source);
    if (source_type == OHSH_PATH_MISSING) {
        print_missing_path(command->source);
        free(source);
        free(destination);
        return 1;
    }

    char *final_destination = resolve_destination_path(source, destination);
    if (final_destination && ohsh_path_type(final_destination) != OHSH_PATH_MISSING) {
        char message[1024];
        snprintf(message, sizeof(message), "Moving will overwrite \"%s\".", command->destination);
        if (!confirm_action(context, command, message)) {
            free(source);
            free(destination);
            free(final_destination);
            return 1;
        }
    }

    if (!final_destination || ohsh_rename(source, final_destination) != 0) {
        if (source_type == OHSH_PATH_FILE && final_destination &&
            ohsh_copy_file(source, final_destination) == 0 && ohsh_delete_file(source) == 0) {
            printf("📦 Moved \"%s\" → \"%s\".\n", command->source, command->destination);
            free(source);
            free(destination);
            free(final_destination);
            return 0;
        }

        printf("I couldn't move \"%s\" to \"%s\": %s\n", command->source, command->destination, ohsh_platform_error());
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

static int delete_bulk_callback(const OhshDirEntry *entry, void *context) {
    DeleteBulkContext *delete_context = context;
    const Command *command = delete_context->command;
    if (entry->name[0] == '.' || !entry->is_file || !names_match_extension(entry->name, command->filter_extension)) return 0;

    if (ohsh_delete_file(entry->name) == 0) delete_context->deleted++;
    else {
        printf("I couldn't delete \"%s\": %s\n", entry->name, ohsh_platform_error());
        delete_context->failed++;
    }
    return 0;
}

static int delete_bulk_files(const Command *command, ShellContext *shell_context) {
    if (!command->filter_extension) {
        printf("Delete what kind of file?\n\nExample:\n  delete every txt file\n");
        return 1;
    }

    char message[1024];
    snprintf(message, sizeof(message), "This will delete every .%s file in the current folder.", command->filter_extension);
    if (!confirm_action(shell_context, command, message)) return 1;

    DeleteBulkContext delete_context;
    delete_context.command = command;
    delete_context.deleted = 0;
    delete_context.failed = 0;
    if (ohsh_list_dir_entries(".", delete_bulk_callback, &delete_context) != 0) {
        printf("I couldn't look through this folder: %s\n", ohsh_platform_error());
        return 1;
    }

    if (delete_context.deleted == 0 && delete_context.failed == 0) {
        printf("🌿 I didn't find any .%s files to delete.\n", command->filter_extension);
    } else if (delete_context.failed == 0) {
        printf("🗑 Deleted %d .%s file%s.\n", delete_context.deleted, command->filter_extension, delete_context.deleted == 1 ? "" : "s");
    }
    return delete_context.failed == 0 ? 0 : 1;
}

static int is_dangerous_delete_target(const char *target) {
    if (!target || target[0] == '\0') return 1;
    if (strcmp(target, "/") == 0 || strcmp(target, "\\") == 0 ||
        strcmp(target, ".") == 0 || strcmp(target, "..") == 0 || strcmp(target, "~") == 0) {
        return 1;
    }

    char *home = ohsh_get_home();
    int dangerous = home && ohsh_is_same_path(target, home);
    ohsh_free(home);
    return dangerous;
}

static int delete_path_command(const Command *command, ShellContext *context) {
    if (command->bulk) return delete_bulk_files(command, context);
    if (is_dangerous_delete_target(command->target)) {
        printf("🛡 I won't delete \"%s\" because that target is too broad or too important.\n", command->target);
        return 1;
    }

    char *target = expand_user_path(command->target);
    if (!target) return 1;

    OhshPathType type = ohsh_path_type(target);
    if (type == OHSH_PATH_MISSING) {
        print_missing_path(command->target);
        free(target);
        return 1;
    }

    if (type == OHSH_PATH_FOLDER) {
        char message[1024];
        snprintf(message, sizeof(message), "This will delete folder \"%s\"%s.", command->target, command->recursive ? " and everything inside it" : "");
        if (!confirm_action(context, command, message)) {
            free(target);
            return 1;
        }

        int result = command->recursive ? ohsh_delete_folder_recursive(target) : ohsh_delete_folder(target);
        if (result != 0) {
            printf("I couldn't delete folder \"%s\": %s\n", command->target, ohsh_platform_error());
            if (!command->recursive) {
                printf("   If it is not empty and you really want to delete everything inside, type:\n");
                printf("   delete %s permanently\n", command->target);
            }
            free(target);
            return 1;
        }
        printf("🗑 Deleted folder \"%s\"%s.\n", command->target, command->recursive ? " permanently" : "");
        free(target);
        return 0;
    }

    char message[1024];
    snprintf(message, sizeof(message), "This will delete \"%s\".", command->target);
    if (!confirm_action(context, command, message)) {
        free(target);
        return 1;
    }

    if (ohsh_delete_file(target) != 0) {
        printf("I couldn't delete \"%s\": %s\n", command->target, ohsh_platform_error());
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
    for (int i = 0; i < context->count; i++) printf("%3d  %s\n", i + 1, context->items[i]);
    return 0;
}

static int set_color(const Command *command, ShellContext *context) {
    if (context && !context->color_enabled) {
        printf("Color output is disabled in your OHSH config.\n");
        return 0;
    }

    if (!command->color) {
        printf("What color should I use?\n\nExample:\n  color cyan\n");
        return 1;
    }

    if (equals_ignore_case(command->color, "red")) printf("\033[31m");
    else if (equals_ignore_case(command->color, "green")) printf("\033[32m");
    else if (equals_ignore_case(command->color, "yellow")) printf("\033[33m");
    else if (equals_ignore_case(command->color, "blue")) printf("\033[34m");
    else if (equals_ignore_case(command->color, "magenta")) printf("\033[35m");
    else if (equals_ignore_case(command->color, "cyan")) printf("\033[36m");
    else if (equals_ignore_case(command->color, "white")) printf("\033[37m");
    else if (equals_ignore_case(command->color, "reset") || equals_ignore_case(command->color, "normal")) printf("\033[0m");
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
    if (!makefile || ohsh_path_type(makefile) != OHSH_PATH_FILE) {
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
    int build_status = ohsh_run_command(build_args, NULL, NULL, 0);
    if (build_status != 0) {
        printf("The rebuild failed. Check the compiler output above.\n");
        return build_status;
    }

    char *source = join_path(OHSH_SRC_DIR, "ohsh");
    const char *destination = "/usr/local/bin/ohsh";
    if (!source || ohsh_copy_file(source, destination) != 0) {
        printf("OHSH was rebuilt, but I couldn't install it to \"%s\": %s\n", destination, ohsh_platform_error());
        printf("You may need to install it with elevated permissions.\n");
        free(source);
        return 1;
    }

    ohsh_make_executable(destination);
    printf("✅ OHSH is up to date.\n");
    free(source);
    return 0;
}

static int run_external_line(const char *line, const ShellContext *context) {
    if (!line || line[0] == '\0') return 0;

    int status = ohsh_run_shell_line(line, context ? context->fallback_shell : NULL);
    if (status < 0) {
        printf("I couldn't run that system command: %s\n", ohsh_platform_error());
        return 1;
    }
    return status;
}

static int run_command_action(const Command *command, ShellContext *context) {
    switch (command->kind) {
        case COMMAND_EMPTY:
            return 0;
        case COMMAND_UNKNOWN:
            printf("%s\n", command->error ? command->error : "I don't understand that command.");
            if (!command->error || !strstr(command->error, "Example:")) print_command_suggestions();
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
            return delete_path_command(command, context);
        case COMMAND_COPY_PATH:
            return copy_path_command(command, context);
        case COMMAND_MOVE_PATH:
            return move_path_command(command, context);
        case COMMAND_READ_FILE:
            return read_file_command(command);
        case COMMAND_CLEAR_SCREEN:
            printf("\033[2J\033[H");
            fflush(stdout);
            return 0;
        case COMMAND_SHOW_HISTORY:
            return show_history(context);
        case COMMAND_COLOR:
            return set_color(command, context);
        case COMMAND_UPDATE:
            return update_ohsh();
        case COMMAND_EXTERNAL:
            return run_external_line(command->name, context);
        case COMMAND_EXIT:
            return 0;
        default:
            printf("I don't recognize that yet.\n");
            print_command_suggestions();
            return 1;
    }
}

static int pipeline_is_external_only(Pipeline pipeline) {
    for (int i = 0; i < pipeline.command_count; i++) {
        if (pipeline.commands[i].kind != COMMAND_EXTERNAL) return 0;
    }
    return pipeline.command_count > 0;
}

static int run_pipeline(Pipeline pipeline) {
    OhshProcessCommand commands[MAX_COMMANDS];
    for (int i = 0; i < pipeline.command_count; i++) {
        if (pipeline.commands[i].kind != COMMAND_EXTERNAL) {
            printf("Portable pipelines currently run external commands only.\n");
            printf("Try the command without a pipe, or pipe traditional commands like: cat file.txt | grep word\n");
            return 1;
        }
        commands[i].argv = pipeline.commands[i].args;
        commands[i].redirect_in = pipeline.commands[i].redirect_in;
        commands[i].redirect_out = pipeline.commands[i].redirect_out;
        commands[i].redirect_append = pipeline.commands[i].redirect_append;
    }

    int status = ohsh_run_pipeline(commands, pipeline.command_count);
    if (status < 0) {
        printf("I couldn't connect those commands: %s\n", ohsh_platform_error());
        return 1;
    }
    return status;
}

ExecutionResult execute(Pipeline pipeline, ShellContext *context) {
    if (pipeline.command_count == 0) return EXECUTION_CONTINUE;

    if (pipeline.command_count > 1) {
        int status = pipeline_is_external_only(pipeline)
            ? run_external_line(pipeline.raw_line, context)
            : run_pipeline(pipeline);
        if (context) context->last_status = status;
        return EXECUTION_CONTINUE;
    }

    Command *command = &pipeline.commands[0];
    if (command->kind == COMMAND_EXIT) {
        printf("bye\n");
        if (context) context->last_status = 0;
        return EXECUTION_EXIT;
    }

    if (command->kind == COMMAND_EXTERNAL) {
        int status = run_external_line(pipeline.raw_line, context);
        if (context) context->last_status = status;
        return EXECUTION_CONTINUE;
    }

    OhshRedirectState *redirect = NULL;
    if (ohsh_begin_redirect(command->redirect_in, command->redirect_out, command->redirect_append, &redirect) != 0) {
        if (command->redirect_in) print_missing_path(command->redirect_in);
        else printf("I couldn't redirect that command: %s\n", ohsh_platform_error());
        ohsh_end_redirect(redirect);
        if (context) context->last_status = 1;
        return EXECUTION_CONTINUE;
    }

    int status = run_command_action(command, context);
    if (context) context->last_status = status;
    ohsh_end_redirect(redirect);
    return EXECUTION_CONTINUE;
}
