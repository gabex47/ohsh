# 🌊 OHSH

**OHSH** is an experimental human-first shell written in C. It keeps the useful parts of a traditional shell, but adds readable commands that feel closer to plain English.

Instead of requiring new users to memorize terse Unix commands, OHSH lets them type commands like:

```ohsh
show files
goto Projects
make folder NewGame
make file README.md
copy README.md to Backup
move logo.png into Assets
delete temp.txt
where am i
show history
```

Traditional system commands still work when OHSH doesn't translate a phrase, so existing tools like `grep`, `cat`, `git`, and `make` remain available.

---

## Table of Contents

- [Features](#features)
- [Build](#build)
- [Install](#install)
  - [Homebrew](#-install-with-homebrew)
  - [From Source](#install-from-source)
- [Run](#run)
- [Scripts](#scripts)
- [Safety](#safety)
- [Configuration](#configuration)
- [Command Examples](#command-examples)
- [Command Reference](#command-reference)
- [Project Structure](#project-structure)
- [Architecture](#architecture)
- [Development](#development)

---

## Features

- Natural language command parsing with multi-word phrases
- Synonyms for common actions
- Welcoming startup screen with current location
- Location-aware prompt
- `examples` command for discoverability
- Grouped help page
- Icon-based file, folder, success, and error output
- Occasional tips after commands
- Quoted filenames such as `"my notes.txt"`
- Friendly errors and path suggestions
- Safe confirmations for deletes and overwrites
- `ohsh run script.osh` for automation
- `.ohshrc` config files and user aliases
- Session command history
- Native filesystem operations for OHSH commands
- Exact fallback to the host shell for traditional commands
- Pipes with `|`
- Input redirection with `<`
- Output redirection with `>` and `>>`
- Colored terminal output

---

## Build

```sh
make
```

This creates the executable in the project root:

- macOS/Linux: `ohsh`
- Windows: `ohsh.exe`

You can also run the explicit target:

```bash
make build
```

On Windows with MinGW/MSYS2:

```bat
mingw32-make
ohsh.exe
mingw32-make clean
```

If your Windows environment provides GNU Make as `make`:

```bat
make
make run
make test
make clean
```

The Makefile selects the correct platform backend automatically:

- macOS/Linux: `src/platform/unix.c`
- Windows: `src/platform/windows.c`

---

## Install

### 🍺 Install with Homebrew

OHSH provides an official Homebrew tap for easy installation.

First, add the tap:

```bash
brew tap gabex47/tap
```

Install the latest development build from the `main` branch:

```bash
brew install --HEAD gabex47/tap/ohsh
```

Or directly:

```bash
brew install --HEAD ohsh
```

#### 🔄 Updating Homebrew Installs

For `--HEAD` installs (tracks the latest `main` commit):

```bash
brew update
brew upgrade ohsh
```

To force a rebuild from source:

```bash
brew reinstall gabex47/tap/ohsh
```

#### ❌ Uninstall

```bash
brew uninstall ohsh
```

To remove the tap:

```bash
brew untap gabex47/tap
```

#### ⚠️ Notes

- `--HEAD` installs always build from the latest commit on `main`.
- Homebrew clones the GitHub repo and builds from source automatically.
- Local changes will **not** affect Homebrew installs unless pushed to GitHub.

### Install from Source

```bash
make build
make install
```

By default this installs to `/usr/local/bin/ohsh`. Override `PREFIX` when needed:

```bash
make install PREFIX="$HOME/.local"
```

Uninstall:

```bash
make uninstall PREFIX="$HOME/.local"
```

---

## Run

```bash
make run
```

Or run directly:

```bash
./ohsh
```

On Windows:

```bat
ohsh.exe
```

You should see:

```
🌊 OHSH
Human-first shell

Type "help" to explore commands.
Type "examples" to see what you can say.

📁 ~/Desktop/ohsh
>
```

---

## Scripts

Run an OHSH script file non-interactively:

```bash
ohsh run script.osh
```

Scripts use the same natural-language commands:

```ohsh
make folder Backup
copy README.md to Backup --yes
show files
```

Destructive script actions require an explicit confirmation flag:

```bash
ohsh run cleanup.osh --yes
```

or per command:

```ohsh
delete temp.txt --yes
move draft.txt to final.txt --force
copy notes.txt to Backup -y
```

Lines beginning with `#` are ignored in scripts.

---

## Safety

OHSH is safe by default:

- `delete` asks before removing files or folders.
- `delete every txt file` asks before bulk deletion.
- `copy` and `move` ask before overwriting an existing destination.
- Broad targets such as `/`, `.`, `..`, and your home folder are blocked.
- Script mode refuses destructive operations unless `--yes`, `-y`, or
  `--force` is present.

Examples:

```ohsh
delete temp.txt
delete temp.txt --yes
copy notes.txt to Backup --force
```

---

## Configuration

OHSH reads config from `~/.ohshrc`, then from a project-local `.ohshrc` if one
exists. The local file can override or add project-specific aliases.

```ini
confirm = true
tips = true
color = true
fallback_shell = /bin/bash

alias docs = goto Documents
alias files = show files
alias cleanup = delete every tmp file
```

Supported settings:

| Setting | Values | Purpose |
| --- | --- | --- |
| `confirm` / `confirm_destructive` | `true`, `false` | Ask before destructive or overwrite actions. |
| `tips` | `true`, `false` | Show occasional usage tips. |
| `color` | `true`, `false` | Allow ANSI color commands. |
| `fallback_shell` | shell path/name | Shell used for unrecognized commands. |
| `alias phrase = replacement` | text | Expand custom phrases before parsing. |

---

## Command Examples

### Navigation

```ohsh
goto Downloads
go to Downloads
open Documents
take me to Desktop
enter Projects
change folder Documents
go home
go back
where am i
```

### Looking Around

```ohsh
show files
show folders
show everything
show files in src
show txt files
show files bigger than 10mb
```

### Creating and Reading

```ohsh
make folder Games
make me a folder called Images
create folder Assets
new file notes.txt
make file "project ideas.txt"
read notes.txt
show file notes.txt
```

### Copying and Moving

```ohsh
copy notes.txt to Backup
duplicate photo.png to Pictures
move report.pdf into Documents
rename draft.txt to final.txt
```

### Deleting

```ohsh
delete temp.txt
remove old.txt
erase scratch.txt
trash draft.txt
delete every txt file
delete OldFolder permanently
```

### Shell Commands

```ohsh
say hello
print hello from OHSH
clear screen
please clear the screen
show history
examples
color cyan
color reset
help
exit
```

### Pipes and Redirection

```ohsh
read notes.txt | grep idea
show files > files.txt
say hello >> log.txt
cat src/main.c | grep include
```

---

## Command Reference

| Intent | Examples |
| --- | --- |
| Go somewhere | `goto Downloads`, `go to Projects`, `open Documents`, `cd src` |
| Show location | `where am i`, `pwd`, `show location` |
| List things | `show files`, `show folders`, `show everything`, `show txt files` |
| Create | `make folder Games`, `create file notes.txt`, `new file "ideas.txt"` |
| Read | `read README.md`, `show contents of notes.txt` |
| Copy | `copy README.md to Backup`, `duplicate notes.txt to notes-copy.txt` |
| Move/rename | `move logo.png into Assets`, `rename draft.txt to final.txt` |
| Delete | `delete temp.txt`, `delete OldFolder permanently --yes` |
| Bulk delete | `delete every txt file --yes` |
| Print | `say hello`, `print hello from OHSH` |
| Shell | `clear screen`, `show history`, `examples`, `help`, `exit` |
| Traditional fallback | `git status`, `cat *.c | grep main`, `make test` |

Traditional fallback commands are sent to the host shell as written, so normal
shell quoting, globs, pipes, and redirection work.

---

## Project Structure

```
src/
  lexer.c      tokenizes words, quotes, pipes, and redirection
  lexer.h      lexer token types
  parser.c     translates natural phrases into typed commands
  parser.h     command AST and parser declarations
  executor.c   dispatches commands through the platform layer
  executor.h   shell context, history, and execution API
  main.c       interactive prompt loop
  platform/
    platform.h shared platform API used by the shell engine
    unix.c     macOS/Linux implementation
    windows.c  Windows implementation
Makefile       build, run, and clean targets
homebrew-tap/  Homebrew tap files for gabex47/homebrew-tap
scripts/       local packaging helpers
```

---

## Architecture

OHSH has a small, explicit pipeline:

1. `main.c` loads config, expands aliases, reads interactive or script input,
   stores session history, and runs the command pipeline.
2. `lexer.c` turns input into tokens while preserving quoted filenames,
   escaped characters, pipes, and redirection.
3. `parser.c` uses a command-pattern registry to map phrases into typed command
   actions such as `COMMAND_LIST`, `COMMAND_COPY_PATH`, and
   `COMMAND_DELETE_PATH`.
4. `executor.c` performs semantic checks before acting: missing paths,
   folder/file mismatches, dangerous delete targets, overwrite checks, and
   confirmation prompts.
5. `src/platform/` owns OS-specific filesystem, process, redirection, and shell
   fallback behavior for macOS, Linux, and Windows.

This keeps the user-facing language flexible while keeping execution explicit,
testable, and easier to extend.

See [MIGRATION.md](MIGRATION.md) for behavior changes from earlier OHSH builds
and [CHANGELOG.md](CHANGELOG.md) for release notes.

## Development

```bash
make
make build
make run
make install
make uninstall
make clean
make test
make sanitize
```

Windows with MinGW/MSYS2:

```bat
mingw32-make
mingw32-make run
mingw32-make test
mingw32-make clean
ohsh.exe
```

Windows when GNU Make is available as `make`:

```bat
make
make run
make test
make clean
ohsh.exe
```
