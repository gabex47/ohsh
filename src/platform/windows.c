#include "platform.h"

#include <direct.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <windows.h>

struct OhshRedirectState {
    int saved_stdin;
    int saved_stdout;
};

static char last_error[256];

static void set_error_from_windows(void) {
    DWORD code = GetLastError();
    FormatMessageA(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        code,
        0,
        last_error,
        (DWORD)sizeof(last_error),
        NULL
    );
    if (last_error[0] == '\0') snprintf(last_error, sizeof(last_error), "Windows error %lu", code);
}

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

static int has_separator(const char *path) {
    return strchr(path, '/') != NULL || strchr(path, '\\') != NULL;
}

static char *join_path(const char *left, const char *right) {
    if (!right) return NULL;
    if (strlen(right) > 2 && right[1] == ':') return dup_string(right);
    if (!left || left[0] == '\0' || strcmp(left, ".") == 0) return dup_string(right);

    size_t left_len = strlen(left);
    size_t right_len = strlen(right);
    int needs_slash = left[left_len - 1] != '/' && left[left_len - 1] != '\\';
    char *joined = malloc(left_len + right_len + (needs_slash ? 2 : 1));
    if (!joined) return NULL;

    strcpy(joined, left);
    if (needs_slash) strcat(joined, "\\");
    strcat(joined, right);
    return joined;
}

int ohsh_cd(const char *path) {
    if (_chdir(path) != 0) {
        set_error_from_errno();
        return -1;
    }
    return 0;
}

int ohsh_mkdir(const char *path) {
    if (!CreateDirectoryA(path, NULL)) {
        set_error_from_windows();
        return -1;
    }
    return 0;
}

int ohsh_delete_file(const char *path) {
    if (!DeleteFileA(path)) {
        set_error_from_windows();
        return -1;
    }
    return 0;
}

int ohsh_delete_folder(const char *path) {
    if (!RemoveDirectoryA(path)) {
        set_error_from_windows();
        return -1;
    }
    return 0;
}

int ohsh_rename(const char *oldpath, const char *newpath) {
    if (!MoveFileA(oldpath, newpath)) {
        set_error_from_windows();
        return -1;
    }
    return 0;
}

char *ohsh_get_cwd(void) {
    char *cwd = _getcwd(NULL, 0);
    if (!cwd) set_error_from_errno();
    return cwd;
}

char *ohsh_get_home(void) {
    const char *home = getenv("USERPROFILE");
    if (!home) home = getenv("HOME");
    return home ? dup_string(home) : NULL;
}

OhshPathType ohsh_path_type(const char *path) {
    DWORD attributes = GetFileAttributesA(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        set_error_from_windows();
        return OHSH_PATH_MISSING;
    }
    if (attributes & FILE_ATTRIBUTE_DIRECTORY) return OHSH_PATH_FOLDER;
    return OHSH_PATH_FILE;
}

int ohsh_is_same_path(const char *left, const char *right) {
    char left_full[MAX_PATH];
    char right_full[MAX_PATH];
    if (!GetFullPathNameA(left, MAX_PATH, left_full, NULL)) return 0;
    if (!GetFullPathNameA(right, MAX_PATH, right_full, NULL)) return 0;
    return _stricmp(left_full, right_full) == 0;
}

int ohsh_list_dir_entries(const char *path, OhshListDirCallback callback, void *context) {
    char *pattern = join_path(path, "*");
    if (!pattern) return -1;

    WIN32_FIND_DATAA data;
    HANDLE handle = FindFirstFileA(pattern, &data);
    free(pattern);

    if (handle == INVALID_HANDLE_VALUE) {
        set_error_from_windows();
        return -1;
    }

    do {
        if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0) continue;

        OhshDirEntry entry;
        entry.name = data.cFileName;
        entry.is_folder = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        entry.is_file = !entry.is_folder;
        entry.size = ((unsigned long long)data.nFileSizeHigh << 32) | data.nFileSizeLow;

        if (callback(&entry, context) != 0) {
            FindClose(handle);
            return -1;
        }
    } while (FindNextFileA(handle, &data));

    FindClose(handle);
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

    char buffer[MAX_PATH];
    if (has_separator(name)) {
        DWORD attributes = GetFileAttributesA(name);
        return attributes != INVALID_FILE_ATTRIBUTES ? dup_string(name) : NULL;
    }

    DWORD length = SearchPathA(NULL, name, ".exe", MAX_PATH, buffer, NULL);
    if (length == 0 || length >= MAX_PATH) return NULL;
    return dup_string(buffer);
}

int ohsh_create_file(const char *path) {
    HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        set_error_from_windows();
        return -1;
    }
    CloseHandle(file);
    return 0;
}

int ohsh_copy_file(const char *source, const char *destination) {
    if (!CopyFileA(source, destination, FALSE)) {
        set_error_from_windows();
        return -1;
    }
    return 0;
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
    char *pattern = join_path(path, "*");
    if (!pattern) return -1;

    WIN32_FIND_DATAA data;
    HANDLE handle = FindFirstFileA(pattern, &data);
    free(pattern);

    if (handle == INVALID_HANDLE_VALUE) {
        set_error_from_windows();
        return -1;
    }

    do {
        if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0) continue;

        char *child = join_path(path, data.cFileName);
        if (!child) {
            FindClose(handle);
            return -1;
        }

        int result = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            ? ohsh_delete_folder_recursive(child)
            : ohsh_delete_file(child);
        free(child);
        if (result != 0) {
            FindClose(handle);
            return -1;
        }
    } while (FindNextFileA(handle, &data));

    FindClose(handle);
    return ohsh_delete_folder(path);
}

int ohsh_make_executable(const char *path) {
    (void)path;
    return 0;
}

int ohsh_begin_redirect(const char *redirect_in, const char *redirect_out, int append, OhshRedirectState **state) {
    OhshRedirectState *redirect = calloc(1, sizeof(*redirect));
    if (!redirect) return -1;
    redirect->saved_stdin = -1;
    redirect->saved_stdout = -1;

    if (redirect_in) {
        int input = _open(redirect_in, _O_RDONLY | _O_BINARY);
        if (input < 0) {
            set_error_from_errno();
            free(redirect);
            return -1;
        }
        redirect->saved_stdin = _dup(_fileno(stdin));
        if (redirect->saved_stdin < 0 || _dup2(input, _fileno(stdin)) != 0) {
            set_error_from_errno();
            _close(input);
            free(redirect);
            return -1;
        }
        _close(input);
    }

    if (redirect_out) {
        int flags = _O_WRONLY | _O_CREAT | _O_BINARY | (append ? _O_APPEND : _O_TRUNC);
        int output = _open(redirect_out, flags, _S_IREAD | _S_IWRITE);
        if (output < 0) {
            set_error_from_errno();
            ohsh_end_redirect(redirect);
            return -1;
        }
        redirect->saved_stdout = _dup(_fileno(stdout));
        if (redirect->saved_stdout < 0 || _dup2(output, _fileno(stdout)) != 0) {
            set_error_from_errno();
            _close(output);
            ohsh_end_redirect(redirect);
            return -1;
        }
        _close(output);
    }

    *state = redirect;
    return 0;
}

void ohsh_end_redirect(OhshRedirectState *state) {
    if (!state) return;
    fflush(stdout);
    if (state->saved_stdin >= 0) {
        _dup2(state->saved_stdin, _fileno(stdin));
        _close(state->saved_stdin);
    }
    if (state->saved_stdout >= 0) {
        _dup2(state->saved_stdout, _fileno(stdout));
        _close(state->saved_stdout);
    }
    free(state);
}

static char *quote_command(char *const argv[]) {
    size_t length = 1;
    for (int i = 0; argv[i]; i++) length += strlen(argv[i]) * 2 + 4;

    char *line = malloc(length);
    if (!line) return NULL;
    line[0] = '\0';

    for (int i = 0; argv[i]; i++) {
        if (i > 0) strcat(line, " ");
        strcat(line, "\"");
        for (const char *p = argv[i]; *p; p++) {
            if (*p == '"') strcat(line, "\\\"");
            else {
                size_t len = strlen(line);
                line[len] = *p;
                line[len + 1] = '\0';
            }
        }
        strcat(line, "\"");
    }
    return line;
}

static HANDLE open_input_handle(const char *path) {
    if (!path) return GetStdHandle(STD_INPUT_HANDLE);
    return CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
}

static HANDLE open_output_handle(const char *path, int append) {
    if (!path) return GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE handle = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                                append ? OPEN_ALWAYS : CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, NULL);
    if (append && handle != INVALID_HANDLE_VALUE) SetFilePointer(handle, 0, NULL, FILE_END);
    return handle;
}

static int spawn_process(char *const argv[], HANDLE input, HANDLE output, PROCESS_INFORMATION *process) {
    char *command = quote_command(argv);
    if (!command) return -1;

    STARTUPINFOA startup;
    ZeroMemory(&startup, sizeof(startup));
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input;
    startup.hStdOutput = output;
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    ZeroMemory(process, sizeof(*process));
    BOOL ok = CreateProcessA(NULL, command, NULL, NULL, TRUE, 0, NULL, NULL, &startup, process);
    free(command);
    if (!ok) {
        set_error_from_windows();
        return -1;
    }
    return 0;
}

int ohsh_run_command(char *const argv[], const char *redirect_in, const char *redirect_out, int append) {
    HANDLE input = open_input_handle(redirect_in);
    HANDLE output = open_output_handle(redirect_out, append);
    if (input == INVALID_HANDLE_VALUE || output == INVALID_HANDLE_VALUE) {
        set_error_from_windows();
        return -1;
    }

    PROCESS_INFORMATION process;
    int result = spawn_process(argv, input, output, &process);
    if (redirect_in) CloseHandle(input);
    if (redirect_out) CloseHandle(output);
    if (result != 0) return -1;

    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exit_code = 1;
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return (int)exit_code;
}

int ohsh_run_pipeline(const OhshProcessCommand *commands, int count) {
    SECURITY_ATTRIBUTES security;
    security.nLength = sizeof(security);
    security.lpSecurityDescriptor = NULL;
    security.bInheritHandle = TRUE;

    PROCESS_INFORMATION *processes = calloc((size_t)count, sizeof(*processes));
    if (!processes) return -1;

    HANDLE previous_read = NULL;
    int result = 0;

    for (int i = 0; i < count; i++) {
        HANDLE read_pipe = NULL;
        HANDLE write_pipe = NULL;
        HANDLE input = previous_read ? previous_read : open_input_handle(commands[i].redirect_in);
        HANDLE output = NULL;

        if (i < count - 1) {
            if (!CreatePipe(&read_pipe, &write_pipe, &security, 0)) {
                set_error_from_windows();
                result = -1;
                break;
            }
            output = write_pipe;
        } else {
            output = open_output_handle(commands[i].redirect_out, commands[i].redirect_append);
        }

        if (input == INVALID_HANDLE_VALUE || output == INVALID_HANDLE_VALUE ||
            spawn_process(commands[i].argv, input, output, &processes[i]) != 0) {
            result = -1;
            break;
        }

        if (previous_read) CloseHandle(previous_read);
        if (write_pipe) CloseHandle(write_pipe);
        previous_read = read_pipe;
    }

    if (previous_read) CloseHandle(previous_read);

    if (result == 0) {
        HANDLE *handles = malloc(sizeof(HANDLE) * (size_t)count);
        if (!handles) result = -1;
        else {
            for (int i = 0; i < count; i++) handles[i] = processes[i].hProcess;
            WaitForMultipleObjects((DWORD)count, handles, TRUE, INFINITE);
            free(handles);
        }
    }

    for (int i = 0; i < count; i++) {
        if (processes[i].hThread) CloseHandle(processes[i].hThread);
        if (processes[i].hProcess) CloseHandle(processes[i].hProcess);
    }
    free(processes);
    return result;
}

const char *ohsh_platform_error(void) {
    return last_error[0] ? last_error : "platform operation failed";
}

void ohsh_free(void *ptr) {
    free(ptr);
}
