# Changelog

## Unreleased

- Added a cross-platform platform layer for filesystem, process, redirection,
  and shell fallback behavior.
- Added `ohsh run script.osh` for non-interactive scripts.
- Added safe-by-default confirmations for deletes and overwrites, with
  `--yes`, `-y`, or `--force` for automation.
- Added `.ohshrc` config loading for confirmations, tips, color output,
  fallback shell selection, and aliases.
- Changed fallback commands to run through the real system shell so quoting,
  globs, pipes, and redirection behave like the host shell.
- Added integration tests and GitHub Actions coverage for Linux, macOS,
  Windows, and sanitizer builds.

## Original

- Interactive human-first shell with natural-language navigation, file
  creation, listing, copy, move, delete, history, help, and examples.
