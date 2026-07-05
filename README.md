# OHSH

OHSH is an experimental human-first shell written in C. It keeps the useful
parts of a traditional shell, but adds readable commands that feel closer to
plain English.

Instead of requiring new users to memorize terse Unix commands, OHSH lets them
type commands like:

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

Traditional system commands still work when OHSH does not translate a phrase,
so existing tools like `grep`, `cat`, `git`, and `make` remain available.

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
- Session command history
- Native filesystem operations for OHSH commands
- External command execution through `PATH`
- Pipelines with `|`
- Input redirection with `<`
- Output redirection with `>` and `>>`
- Colored terminal output

## Build

```sh
make build
```

This creates the `ohsh` executable in the project root.

## Install with Homebrew

OHSH includes a ready-to-publish tap template in `homebrew-tap/`.

After creating the GitHub repository `gabex47/homebrew-tap` and pushing the
contents of `homebrew-tap/` to it, install the latest development build from the
`main` branch with:

```sh
brew install --HEAD gabex47/tap/ohsh
```

Or tap first:

```sh
brew tap gabex47/tap
brew install --HEAD ohsh
```

Stable Homebrew installs require a tagged OHSH release and a formula URL with a
SHA256 checksum. Once a stable formula is generated in the tap, users can run:

```sh
brew tap gabex47/tap
brew install ohsh
```

## Updating Homebrew Installs

For a HEAD install:

```sh
brew update
brew reinstall --HEAD gabex47/tap/ohsh
```

For a stable install:

```sh
brew update
brew upgrade ohsh
```

## Uninstalling Homebrew Installs

```sh
brew uninstall ohsh
```

To remove the tap too:

```sh
brew untap gabex47/tap
```

## Install from Source

```sh
make
make install
```

By default this installs to `/usr/local/bin/ohsh`. Override `PREFIX` when
needed:

```sh
make install PREFIX="$HOME/.local"
```

Uninstall from the same prefix:

```sh
make uninstall PREFIX="$HOME/.local"
```

## Run

```sh
make run
```

Or run the binary directly:

```sh
./ohsh
```

You should see:

```text
🌊 OHSH v0.3
Human-first shell

Type "help" to explore commands.
Type "examples" to see what you can say.

📁 ~/Desktop/ohsh

📁 ~/Desktop/ohsh
>
```

## Command Examples

Navigation:

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

Looking around:

```ohsh
show files
show folders
show everything
show files in src
show txt files
show files bigger than 10mb
```

Creating and reading:

```ohsh
make folder Games
make me a folder called Images
create folder Assets
new file notes.txt
make file "project ideas.txt"
read notes.txt
show file notes.txt
```

Copying and moving:

```ohsh
copy notes.txt to Backup
duplicate photo.png to Pictures
move report.pdf into Documents
rename draft.txt to final.txt
```

Deleting:

```ohsh
delete temp.txt
remove old.txt
erase scratch.txt
trash draft.txt
delete every txt file
delete OldFolder permanently
```

Shell commands:

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

Pipes and redirection:

```ohsh
read notes.txt | grep idea
show files > files.txt
say hello >> log.txt
cat src/main.c | grep include
```

## Project Structure

```text
src/
  lexer.c      tokenizes words, quotes, pipes, and redirection
  lexer.h      lexer token types
  parser.c     translates natural phrases into typed commands
  parser.h     command AST and parser declarations
  executor.c   dispatches commands and performs native filesystem work
  executor.h   shell context, history, and execution API
  main.c       interactive prompt loop
Makefile       build, run, and clean targets
README.md      project documentation
homebrew-tap/  Homebrew tap files for gabex47/homebrew-tap
scripts/       local packaging verification helpers
```

## Architecture

OHSH has four small layers:

1. `main.c` reads a line, stores it in session history, and runs the pipeline.
2. `lexer.c` turns input into tokens while preserving quoted filenames.
3. `parser.c` uses a command-pattern registry to map phrases into typed command
   actions such as `COMMAND_LIST`, `COMMAND_COPY_PATH`, and
   `COMMAND_DELETE_PATH`.
4. `executor.c` dispatches those actions. OHSH commands use native C APIs like
   `chdir`, `mkdir`, `open`, `rename`, `unlink`, `opendir`, `readdir`, and
   `stat`. It also owns the startup screen, prompt, help, examples, tips, and
   friendly output formatting. Unknown commands fall back to normal system
   command execution.

This keeps the user-facing language flexible while keeping execution explicit,
testable, and easier to extend.

## Development

Useful commands:

```sh
make
make build
make run
make install
make uninstall
make clean
make test
```

When adding commands, prefer adding a focused parser handler and executor action
instead of special-casing behavior in the prompt loop. Destructive operations
should keep friendly guardrails, clear error messages, and native filesystem
calls wherever possible.

## Homebrew Tap Maintenance

The generated tap lives in `homebrew-tap/` so it can be copied or pushed to a
separate GitHub repository named `homebrew-tap`.

Validate the HEAD formula locally with:

```sh
./scripts/verify-homebrew-head.sh
```

If `ohsh` is already installed with Homebrew, the script stops before replacing
it. To allow a fresh uninstall/install verification:

```sh
OHSH_HOMEBREW_REINSTALL=1 ./scripts/verify-homebrew-head.sh
```

After publishing a tagged release, generate stable formula metadata with:

```sh
cd homebrew-tap
./scripts/update-stable-formula.sh v0.3.0
```

Commit and push the updated `Formula/ohsh.rb` in `gabex47/homebrew-tap`.
