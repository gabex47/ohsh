#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "parser.h"

typedef int (*ParseHandler)(Command *command, char **words, int count);

typedef struct {
    const char *name;
    ParseHandler handler;
} CommandPattern;

static char *dup_string(const char *value) {
    size_t length = strlen(value) + 1;
    char *copy = malloc(length);
    if (!copy) return NULL;
    memcpy(copy, value, length);
    return copy;
}

static void init_command(Command *command) {
    memset(command, 0, sizeof(*command));
    command->kind = COMMAND_EMPTY;
    command->list_mode = LIST_EVERYTHING;
}

static int is_word(const char *word, const char *expected) {
    return word && expected && strcasecmp(word, expected) == 0;
}

static int is_any_word(const char *word, const char *const *options) {
    for (int i = 0; options[i] != NULL; i++) {
        if (is_word(word, options[i])) return 1;
    }
    return 0;
}

static int is_polite_word(const char *word) {
    static const char *const words[] = {
        "please", "kindly", "just", NULL
    };
    return is_any_word(word, words);
}

static int is_soft_filler_word(const char *word) {
    static const char *const words[] = {
        "please", "kindly", "just", "can", "could", "would", "will", "you", NULL
    };
    return is_any_word(word, words);
}

static int is_self_word(const char *word) {
    static const char *const words[] = {
        "me", "myself", "us", NULL
    };
    return is_any_word(word, words);
}

static int is_called_word(const char *word) {
    static const char *const words[] = {
        "called", "named", "name", NULL
    };
    return is_any_word(word, words);
}

static int is_article(const char *word) {
    static const char *const words[] = {
        "the", "a", "an", "my", NULL
    };
    return is_any_word(word, words);
}

static int is_path_descriptor(const char *word) {
    static const char *const words[] = {
        "file", "files", "folder", "folders", "directory", "directories", "dir", "document", NULL
    };
    return is_any_word(word, words);
}

static int is_preposition(const char *word) {
    static const char *const words[] = {
        "to", "into", "in", "inside", "onto", "as", NULL
    };
    return is_any_word(word, words);
}

static int is_permanent_word(const char *word) {
    static const char *const words[] = {
        "permanent", "permanently", "forever", "force", "fully", NULL
    };
    return is_any_word(word, words);
}

static char *join_words(char **words, int start, int end) {
    if (start >= end) return NULL;

    size_t length = 0;
    for (int i = start; i < end; i++) {
        length += strlen(words[i]) + 1;
    }

    char *joined = malloc(length);
    if (!joined) return NULL;
    joined[0] = '\0';

    for (int i = start; i < end; i++) {
        if (i > start) strcat(joined, " ");
        strcat(joined, words[i]);
    }

    return joined;
}

static char *join_path_words(char **words, int start, int end) {
    while (start < end && is_article(words[start])) start++;

    if (end - start > 1 && is_path_descriptor(words[start])) {
        start++;
        while (start < end && is_article(words[start])) start++;
    }

    while (end > start && (is_article(words[end - 1]) || is_permanent_word(words[end - 1]))) {
        end--;
    }

    return join_words(words, start, end);
}

static void set_error(Command *command, const char *message) {
    command->kind = COMMAND_UNKNOWN;
    free(command->error);
    command->error = dup_string(message);
}

static void set_external(Command *command, char **words, int count) {
    command->kind = COMMAND_EXTERNAL;
    command->name = dup_string(words[0]);

    int limit = count < MAX_ARGS - 1 ? count : MAX_ARGS - 1;
    for (int i = 0; i < limit; i++) {
        command->args[i] = dup_string(words[i]);
    }
    command->args[limit] = NULL;
    command->arg_count = limit;
}

static char *extension_from_word(const char *word) {
    if (!word || word[0] == '\0') return NULL;

    while (*word == '*') word++;
    if (*word == '.') word++;
    if (*word == '\0') return NULL;

    return dup_string(word);
}

static int parse_size_value(char **words, int count, int index, size_t *bytes, int *consumed) {
    if (index >= count) return 0;

    char *end = NULL;
    double value = strtod(words[index], &end);
    if (end == words[index] || value < 0) return 0;

    const char *unit = end;
    int used = 1;

    if (unit[0] == '\0' && index + 1 < count) {
        unit = words[index + 1];
        used = 2;
    }

    double multiplier = 1.0;
    if (unit[0] == '\0' ||
        is_word(unit, "b") ||
        is_word(unit, "byte") ||
        is_word(unit, "bytes")) {
        multiplier = 1.0;
    } else if (is_word(unit, "k") || is_word(unit, "kb") || is_word(unit, "kilobyte") || is_word(unit, "kilobytes")) {
        multiplier = 1024.0;
    } else if (is_word(unit, "m") || is_word(unit, "mb") || is_word(unit, "megabyte") || is_word(unit, "megabytes")) {
        multiplier = 1024.0 * 1024.0;
    } else if (is_word(unit, "g") || is_word(unit, "gb") || is_word(unit, "gigabyte") || is_word(unit, "gigabytes")) {
        multiplier = 1024.0 * 1024.0 * 1024.0;
    } else {
        return 0;
    }

    *bytes = (size_t)(value * multiplier);
    *consumed = used;
    return 1;
}

static int find_separator(char **words, int start, int count) {
    for (int i = start; i < count; i++) {
        if (is_preposition(words[i])) return i;
    }
    return -1;
}

static int parse_help(Command *command, char **words, int count) {
    if ((count == 1 && (is_word(words[0], "help") || is_word(words[0], "?") || is_word(words[0], "commands"))) ||
        (count == 2 && is_word(words[0], "show") && is_word(words[1], "help")) ||
        (count == 2 && is_word(words[0], "list") && is_word(words[1], "commands")) ||
        (count >= 4 && is_word(words[0], "what") && is_word(words[1], "can") && is_word(words[2], "i") && is_word(words[3], "do"))) {
        command->kind = COMMAND_HELP;
        return 1;
    }
    return 0;
}

static int parse_examples(Command *command, char **words, int count) {
    if ((count == 1 && (is_word(words[0], "examples") || is_word(words[0], "ideas"))) ||
        (count == 2 && is_word(words[0], "show") && (is_word(words[1], "examples") || is_word(words[1], "ideas"))) ||
        (count == 2 && is_word(words[0], "give") && is_word(words[1], "examples")) ||
        (count >= 3 && is_word(words[0], "what") && is_word(words[1], "can") && is_word(words[2], "say"))) {
        command->kind = COMMAND_EXAMPLES;
        return 1;
    }
    return 0;
}

static int parse_exit(Command *command, char **words, int count) {
    static const char *const exits[] = {
        "exit", "quit", "leave", "logout", "bye", "goodbye", NULL
    };

    if (count == 1 && is_any_word(words[0], exits)) {
        command->kind = COMMAND_EXIT;
        return 1;
    }
    return 0;
}

static int parse_history(Command *command, char **words, int count) {
    if ((count == 1 && is_word(words[0], "history")) ||
        (count == 2 && is_word(words[0], "show") && is_word(words[1], "history")) ||
        (count == 2 && is_word(words[0], "show") && is_word(words[1], "commands")) ||
        (count == 2 && is_word(words[0], "recent") && is_word(words[1], "commands")) ||
        (count >= 4 && is_word(words[0], "what") && is_word(words[1], "did") && is_word(words[2], "i") && is_word(words[3], "do"))) {
        command->kind = COMMAND_SHOW_HISTORY;
        return 1;
    }
    return 0;
}

static int parse_where(Command *command, char **words, int count) {
    if ((count == 1 && (is_word(words[0], "pwd") || is_word(words[0], "whereami"))) ||
        (count == 1 && is_word(words[0], "where")) ||
        (count == 3 && is_word(words[0], "where") && is_word(words[1], "am") && is_word(words[2], "i")) ||
        (count == 2 && is_word(words[0], "current") && (is_word(words[1], "folder") || is_word(words[1], "directory"))) ||
        (count == 2 && is_word(words[0], "show") && is_word(words[1], "location")) ||
        (count >= 5 && is_word(words[0], "tell") && is_word(words[1], "me") && is_word(words[2], "where") && is_word(words[3], "i") && is_word(words[4], "am"))) {
        command->kind = COMMAND_WHERE_AM_I;
        return 1;
    }
    return 0;
}

static int parse_clear(Command *command, char **words, int count) {
    if ((count == 1 && (is_word(words[0], "clear") || is_word(words[0], "cls"))) ||
        (count >= 2 && is_word(words[0], "clear") && is_word(words[count - 1], "screen")) ||
        (count >= 2 && is_word(words[0], "clean") && is_word(words[count - 1], "screen")) ||
        (count >= 2 && is_word(words[0], "wipe") && is_word(words[count - 1], "screen")) ||
        (count == 2 && is_word(words[0], "clear") && is_word(words[1], "screen")) ||
        (count == 2 && is_word(words[0], "clean") && is_word(words[1], "screen")) ||
        (count == 2 && is_word(words[0], "wipe") && is_word(words[1], "screen"))) {
        command->kind = COMMAND_CLEAR_SCREEN;
        return 1;
    }
    return 0;
}

static int parse_color(Command *command, char **words, int count) {
    if (count < 2) return 0;

    int index = -1;
    if (is_word(words[0], "color") || is_word(words[0], "colour")) {
        index = 1;
    } else if (count >= 3 && is_word(words[0], "set") && (is_word(words[1], "color") || is_word(words[1], "colour"))) {
        index = 2;
    } else if (count >= 4 && is_word(words[0], "set") && is_word(words[1], "text") &&
               (is_word(words[2], "color") || is_word(words[2], "colour"))) {
        index = 3;
    } else if (count >= 4 && is_word(words[0], "change") &&
               (is_word(words[1], "color") || is_word(words[1], "colour")) && is_word(words[2], "to")) {
        index = 3;
    }

    if (index < 0 || index >= count) return 0;

    command->kind = COMMAND_COLOR;
    command->color = dup_string(words[index]);
    return 1;
}

static int parse_update(Command *command, char **words, int count) {
    if ((count == 1 && is_word(words[0], "update")) ||
        (count == 2 && is_word(words[0], "update") && is_word(words[1], "ohsh")) ||
        (count == 1 && is_word(words[0], "rebuild")) ||
        (count == 2 && is_word(words[0], "rebuild") && is_word(words[1], "ohsh")) ||
        (count == 2 && is_word(words[0], "reinstall") && is_word(words[1], "ohsh"))) {
        command->kind = COMMAND_UPDATE;
        return 1;
    }
    return 0;
}

static int parse_change_dir(Command *command, char **words, int count) {
    int index = -1;

    if (count == 1 && (is_word(words[0], "home") || is_word(words[0], "~"))) {
        command->kind = COMMAND_CHANGE_DIR;
        command->target = dup_string("~");
        return 1;
    }

    if (count == 1 && (is_word(words[0], "back") || is_word(words[0], "up"))) {
        command->kind = COMMAND_CHANGE_DIR;
        command->target = dup_string("..");
        return 1;
    }

    if (is_word(words[0], "cd")) {
        command->kind = COMMAND_CHANGE_DIR;
        command->target = count > 1 ? join_path_words(words, 1, count) : dup_string("~");
        return 1;
    }

    if (is_word(words[0], "goto") || is_word(words[0], "go") || is_word(words[0], "enter")) {
        index = 1;
        if (index < count && (is_word(words[index], "to") || is_word(words[index], "into"))) index++;
        if (index < count && (is_word(words[index], "folder") || is_word(words[index], "directory") || is_word(words[index], "dir"))) index++;
    } else if (count >= 2 && is_word(words[0], "open") &&
               !is_path_descriptor(words[1]) && !is_word(words[1], "file")) {
        index = 1;
    } else if (count >= 4 && is_word(words[0], "take") && is_self_word(words[1]) &&
               is_word(words[2], "to")) {
        index = 3;
    } else if (count >= 5 && is_word(words[0], "bring") && is_self_word(words[1]) &&
               is_word(words[2], "to")) {
        index = 3;
    } else if (count >= 2 && is_word(words[0], "change")) {
        index = 1;
        if (index < count && (is_word(words[index], "folder") || is_word(words[index], "directory") || is_word(words[index], "dir"))) index++;
        if (index < count && is_word(words[index], "to")) index++;
    } else if (count >= 2 && is_word(words[0], "switch") && is_word(words[1], "to")) {
        index = 2;
    }

    if (index < 0) return 0;

    if (index >= count) {
        set_error(command, "Where should I go? Try: goto Downloads");
        return 1;
    }

    if (is_word(words[index], "home")) {
        command->kind = COMMAND_CHANGE_DIR;
        command->target = dup_string("~");
        return 1;
    }

    if (is_word(words[index], "back") || is_word(words[index], "up")) {
        command->kind = COMMAND_CHANGE_DIR;
        command->target = dup_string("..");
        return 1;
    }

    command->kind = COMMAND_CHANGE_DIR;
    command->target = join_path_words(words, index, count);
    return 1;
}

static int parse_make(Command *command, char **words, int count) {
    int index = -1;
    CommandKind kind = COMMAND_EMPTY;

    if (is_word(words[0], "mkdir")) {
        kind = COMMAND_MAKE_FOLDER;
        index = 1;
    } else if (is_word(words[0], "touch")) {
        kind = COMMAND_MAKE_FILE;
        index = 1;
    } else if (is_word(words[0], "make") || is_word(words[0], "create") || is_word(words[0], "new")) {
        index = 1;
        if (index < count && is_self_word(words[index])) index++;
        while (index < count && is_article(words[index])) index++;

        if (index < count && (is_word(words[index], "folder") || is_word(words[index], "directory") || is_word(words[index], "dir"))) {
            kind = COMMAND_MAKE_FOLDER;
            index++;
        } else if (index < count && (is_word(words[index], "file") || is_word(words[index], "document"))) {
            kind = COMMAND_MAKE_FILE;
            index++;
        }
    }

    if (index < 0 || kind == COMMAND_EMPTY) return 0;

    while (index < count && (is_article(words[index]) || is_called_word(words[index]))) {
        index++;
    }

    command->kind = kind;
    command->target = join_path_words(words, index, count);

    if (!command->target) {
        set_error(command, kind == COMMAND_MAKE_FOLDER
            ? "What should I name the folder? Try: make folder Projects"
            : "What should I name the file? Try: make file notes.txt");
    }

    return 1;
}

static int parse_copy(Command *command, char **words, int count) {
    static const char *const verbs[] = {
        "copy", "duplicate", "clone", NULL
    };

    if (!is_any_word(words[0], verbs)) return 0;

    command->kind = COMMAND_COPY_PATH;

    if (count == 1) {
        set_error(command, "Copy what?\n\nExample:\n  copy notes.txt to Backup");
        return 1;
    }

    int separator = find_separator(words, 1, count);
    if (separator < 0 && count == 3) {
        command->source = join_path_words(words, 1, 2);
        command->destination = join_path_words(words, 2, 3);
        return 1;
    }

    if (separator <= 1 || separator >= count - 1) {
        set_error(command, "Copy needs a source and a destination. Try: copy photo.png to Pictures");
        return 1;
    }

    command->source = join_path_words(words, 1, separator);
    command->destination = join_path_words(words, separator + 1, count);

    if (!command->source || !command->destination) {
        set_error(command, "Copy needs a source and a destination. Try: copy photo.png to Pictures");
    }

    return 1;
}

static int parse_move(Command *command, char **words, int count) {
    static const char *const verbs[] = {
        "move", "rename", "put", NULL
    };

    if (!is_any_word(words[0], verbs)) return 0;

    command->kind = COMMAND_MOVE_PATH;

    if (count == 1) {
        set_error(command, "Move what?\n\nExample:\n  move report.pdf into Documents");
        return 1;
    }

    int separator = find_separator(words, 1, count);
    if (separator < 0 && count == 3) {
        command->source = join_path_words(words, 1, 2);
        command->destination = join_path_words(words, 2, 3);
        return 1;
    }

    if (separator <= 1 || separator >= count - 1) {
        set_error(command, "Move needs a source and a destination. Try: move report.pdf into Documents");
        return 1;
    }

    command->source = join_path_words(words, 1, separator);
    command->destination = join_path_words(words, separator + 1, count);

    if (!command->source || !command->destination) {
        set_error(command, "Move needs a source and a destination. Try: move report.pdf into Documents");
    }

    return 1;
}

static int parse_delete(Command *command, char **words, int count) {
    static const char *const verbs[] = {
        "delete", "remove", "erase", "trash", "discard", NULL
    };

    if (!is_any_word(words[0], verbs)) return 0;

    command->kind = COMMAND_DELETE_PATH;
    command->recursive = 0;
    command->bulk = 0;

    if (count == 1) {
        set_error(command, "Delete what?\n\nExample:\n  delete temp.txt");
        return 1;
    }

    for (int i = 1; i < count; i++) {
        if (is_permanent_word(words[i])) command->recursive = 1;
    }

    int index = 1;
    while (index < count && is_article(words[index])) index++;

    if (index < count && (is_word(words[index], "all") || is_word(words[index], "every"))) {
        command->bulk = 1;
        index++;

        while (index < count && is_article(words[index])) index++;

        if (index < count && !is_path_descriptor(words[index]) && !is_permanent_word(words[index])) {
            command->filter_extension = extension_from_word(words[index]);
            index++;
        }

        command->target = dup_string(".");
        return 1;
    }

    command->target = join_path_words(words, index, count);
    if (!command->target) {
        set_error(command, "What should I delete? Try: delete temp.txt");
    }

    return 1;
}

static int parse_read(Command *command, char **words, int count) {
    int index = -1;

    if (is_word(words[0], "read")) {
        index = 1;
    } else if (count >= 2 && (is_word(words[0], "view") || is_word(words[0], "open")) &&
               (is_word(words[1], "file") || is_word(words[1], "document"))) {
        index = 2;
    } else if (count >= 2 && is_word(words[0], "show") &&
               (is_word(words[1], "file") || is_word(words[1], "document"))) {
        index = 2;
    } else if (count >= 3 && is_word(words[0], "show") && is_word(words[1], "contents")) {
        index = is_word(words[2], "of") ? 3 : 2;
    }

    if (index < 0) return 0;

    command->kind = COMMAND_READ_FILE;
    command->target = join_path_words(words, index, count);

    if (!command->target) {
        set_error(command, "What file should I read? Try: read notes.txt");
    }

    return 1;
}

static int parse_list(Command *command, char **words, int count) {
    static const char *const verbs[] = {
        "show", "list", "see", "view", "look", NULL
    };

    int index = 0;
    int matched = 0;

    if (is_any_word(words[0], verbs)) {
        matched = 1;
        index = 1;
        if (index < count && is_word(words[index], "me")) index++;
        if (is_word(words[0], "look") && index < count && is_word(words[index], "around")) index++;
    } else if (is_path_descriptor(words[0]) || is_word(words[0], "everything") || is_word(words[0], "all")) {
        matched = 1;
    }

    if (!matched) return 0;

    command->kind = COMMAND_LIST;
    command->list_mode = LIST_EVERYTHING;
    command->include_hidden = 0;

    while (index < count && (is_article(words[index]) || is_word(words[index], "all"))) {
        if (is_word(words[index], "all")) command->include_hidden = 1;
        index++;
    }

    if (index < count && is_word(words[index], "hidden")) {
        command->include_hidden = 1;
        index++;
    }

    if (index < count && (is_word(words[index], "file") || is_word(words[index], "files"))) {
        command->list_mode = LIST_FILES;
        index++;
    } else if (index < count && (is_word(words[index], "folder") || is_word(words[index], "folders") ||
               is_word(words[index], "directory") || is_word(words[index], "directories") || is_word(words[index], "dirs"))) {
        command->list_mode = LIST_FOLDERS;
        index++;
    } else if (index < count && (is_word(words[index], "everything") || is_word(words[index], "all") ||
               is_word(words[index], "contents") || is_word(words[index], "here") || is_word(words[index], "around"))) {
        command->list_mode = LIST_EVERYTHING;
        command->include_hidden = 1;
        index++;
    } else if (index + 1 < count && (is_word(words[index + 1], "file") || is_word(words[index + 1], "files"))) {
        command->list_mode = LIST_FILES;
        command->filter_extension = extension_from_word(words[index]);
        index += 2;
    }

    for (int i = index; i < count; i++) {
        if (is_word(words[i], "in") || is_word(words[i], "inside") || is_word(words[i], "from")) {
            command->target = join_path_words(words, i + 1, count);
            break;
        }

        if (is_word(words[i], "hidden")) {
            command->include_hidden = 1;
            continue;
        }

        if (is_word(words[i], "bigger") || is_word(words[i], "larger") ||
            is_word(words[i], "greater") || is_word(words[i], "over") || is_word(words[i], "above")) {
            int size_index = i + 1;
            if (size_index < count && is_word(words[size_index], "than")) size_index++;

            size_t bytes = 0;
            int consumed = 0;
            if (parse_size_value(words, count, size_index, &bytes, &consumed)) {
                command->has_min_size = 1;
                command->min_size_bytes = bytes;
                i = size_index + consumed - 1;
            }
        }
    }

    return 1;
}

static int parse_print(Command *command, char **words, int count) {
    static const char *const verbs[] = {
        "echo", "say", "print", NULL
    };

    if (!is_any_word(words[0], verbs)) return 0;

    command->kind = COMMAND_PRINT_TEXT;
    command->text = count > 1 ? join_words(words, 1, count) : dup_string("");
    return 1;
}

static CommandPattern patterns[] = {
    {"examples", parse_examples},
    {"help", parse_help},
    {"exit", parse_exit},
    {"history", parse_history},
    {"where", parse_where},
    {"clear", parse_clear},
    {"color", parse_color},
    {"update", parse_update},
    {"change-directory", parse_change_dir},
    {"make", parse_make},
    {"copy", parse_copy},
    {"move", parse_move},
    {"delete", parse_delete},
    {"read", parse_read},
    {"list", parse_list},
    {"print", parse_print},
    {NULL, NULL}
};

static Command parse_words(char **words, int count) {
    Command command;
    init_command(&command);

    int start = 0;
    int end = count;

    while (start < end && is_soft_filler_word(words[start])) start++;
    while (end > start && is_polite_word(words[end - 1])) end--;

    if (start >= end) return command;

    char *trimmed[MAX_ARGS];
    int trimmed_count = 0;
    for (int i = start; i < end && trimmed_count < MAX_ARGS; i++) {
        trimmed[trimmed_count++] = words[i];
    }

    for (int i = 0; patterns[i].handler != NULL; i++) {
        if (patterns[i].handler(&command, trimmed, trimmed_count)) {
            return command;
        }
    }

    set_external(&command, trimmed, trimmed_count);
    return command;
}

static void attach_redirects(Command *command, char *redirect_in, char *redirect_out, int append) {
    command->redirect_in = redirect_in ? dup_string(redirect_in) : NULL;
    command->redirect_out = redirect_out ? dup_string(redirect_out) : NULL;
    command->redirect_append = append;
}

static void add_unknown_pipeline_error(Pipeline *pipeline, const char *message) {
    if (pipeline->command_count >= MAX_COMMANDS) return;

    Command command;
    init_command(&command);
    set_error(&command, message);
    pipeline->commands[pipeline->command_count++] = command;
}

Pipeline parse(TokenList list) {
    Pipeline pipeline;
    pipeline.command_count = 0;

    if (list.error) {
        add_unknown_pipeline_error(&pipeline, list.error);
        return pipeline;
    }

    char *words[MAX_ARGS];
    int word_count = 0;
    char *redirect_in = NULL;
    char *redirect_out = NULL;
    int redirect_append = 0;
    int segment_has_error = 0;

    for (int i = 0; i < list.count; i++) {
        Token token = list.tokens[i];

        if (token.type == TOKEN_WORD) {
            if (word_count < MAX_ARGS) {
                words[word_count++] = token.value;
            } else {
                segment_has_error = 1;
            }
            continue;
        }

        if (token.type == TOKEN_REDIRECT_OUT || token.type == TOKEN_REDIRECT_APPEND || token.type == TOKEN_REDIRECT_IN) {
            if (i + 1 >= list.count || list.tokens[i + 1].type != TOKEN_WORD) {
                segment_has_error = 1;
                continue;
            }

            if (token.type == TOKEN_REDIRECT_IN) {
                redirect_in = list.tokens[i + 1].value;
            } else {
                redirect_out = list.tokens[i + 1].value;
                redirect_append = token.type == TOKEN_REDIRECT_APPEND;
            }
            i++;
            continue;
        }

        if (token.type == TOKEN_PIPE || token.type == TOKEN_EOF) {
            if (pipeline.command_count >= MAX_COMMANDS) {
                break;
            }

            if (segment_has_error) {
                add_unknown_pipeline_error(&pipeline, "I had trouble reading that command. Check the wording and redirection.");
            } else if (word_count > 0) {
                Command command = parse_words(words, word_count);
                attach_redirects(&command, redirect_in, redirect_out, redirect_append);
                pipeline.commands[pipeline.command_count++] = command;
            } else if (token.type == TOKEN_PIPE) {
                add_unknown_pipeline_error(&pipeline, "I found a pipe with no command beside it.");
            }

            word_count = 0;
            redirect_in = NULL;
            redirect_out = NULL;
            redirect_append = 0;
            segment_has_error = 0;
        }
    }

    return pipeline;
}

void free_pipeline(Pipeline *pipeline) {
    if (!pipeline) return;

    for (int i = 0; i < pipeline->command_count; i++) {
        Command *command = &pipeline->commands[i];

        free(command->name);
        for (int arg = 0; arg < command->arg_count; arg++) {
            free(command->args[arg]);
        }

        free(command->target);
        free(command->source);
        free(command->destination);
        free(command->text);
        free(command->color);
        free(command->filter_extension);
        free(command->error);
        free(command->redirect_out);
        free(command->redirect_in);
    }

    pipeline->command_count = 0;
}

const char *command_kind_name(CommandKind kind) {
    switch (kind) {
        case COMMAND_EMPTY: return "empty";
        case COMMAND_EXTERNAL: return "external";
        case COMMAND_HELP: return "help";
        case COMMAND_EXIT: return "exit";
        case COMMAND_CHANGE_DIR: return "change-directory";
        case COMMAND_PRINT_TEXT: return "print-text";
        case COMMAND_WHERE_AM_I: return "where-am-i";
        case COMMAND_LIST: return "list";
        case COMMAND_MAKE_FOLDER: return "make-folder";
        case COMMAND_MAKE_FILE: return "make-file";
        case COMMAND_DELETE_PATH: return "delete-path";
        case COMMAND_COPY_PATH: return "copy-path";
        case COMMAND_MOVE_PATH: return "move-path";
        case COMMAND_READ_FILE: return "read-file";
        case COMMAND_CLEAR_SCREEN: return "clear-screen";
        case COMMAND_SHOW_HISTORY: return "show-history";
        case COMMAND_EXAMPLES: return "examples";
        case COMMAND_COLOR: return "color";
        case COMMAND_UPDATE: return "update";
        case COMMAND_UNKNOWN: return "unknown";
        default: return "unknown";
    }
}
