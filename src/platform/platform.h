#ifndef OHSH_PLATFORM_H
#define OHSH_PLATFORM_H

#include <stddef.h>

typedef enum {
    OHSH_PATH_MISSING = 0,
    OHSH_PATH_FILE,
    OHSH_PATH_FOLDER,
    OHSH_PATH_OTHER
} OhshPathType;

typedef struct {
    const char *name;
    int is_file;
    int is_folder;
    unsigned long long size;
} OhshDirEntry;

typedef int (*OhshListDirCallback)(const OhshDirEntry *entry, void *context);

typedef struct OhshRedirectState OhshRedirectState;

typedef struct {
    char *const *argv;
    const char *redirect_in;
    const char *redirect_out;
    int redirect_append;
} OhshProcessCommand;

/* Required cross-platform filesystem API. Functions return 0 on success and -1 on failure. */
int ohsh_cd(const char *path);
int ohsh_mkdir(const char *path);
int ohsh_delete_file(const char *path);
int ohsh_delete_folder(const char *path);
int ohsh_rename(const char *oldpath, const char *newpath);
int ohsh_list_dir(const char *path);
char *ohsh_get_cwd(void);

/* Shared platform helpers used by the shell engine without exposing OS headers. */
int ohsh_list_dir_entries(const char *path, OhshListDirCallback callback, void *context);
OhshPathType ohsh_path_type(const char *path);
int ohsh_is_same_path(const char *left, const char *right);
char *ohsh_get_home(void);
char *ohsh_find_executable(const char *name);
int ohsh_create_file(const char *path);
int ohsh_copy_file(const char *source, const char *destination);
int ohsh_read_file_to_stdout(const char *path);
int ohsh_delete_folder_recursive(const char *path);
int ohsh_make_executable(const char *path);
int ohsh_begin_redirect(const char *redirect_in, const char *redirect_out, int append, OhshRedirectState **state);
void ohsh_end_redirect(OhshRedirectState *state);
int ohsh_run_command(char *const argv[], const char *redirect_in, const char *redirect_out, int append);
int ohsh_run_pipeline(const OhshProcessCommand *commands, int count);
int ohsh_run_shell_line(const char *line, const char *preferred_shell);
const char *ohsh_platform_error(void);
void ohsh_free(void *ptr);

#endif
