# Migrating From Earlier OHSH Versions

OHSH keeps the existing natural commands, including:

```ohsh
goto Projects
show files
make folder Games
make file README.md
copy README.md to Backup
move logo.png into Assets
delete temp.txt
where am i
```

The main behavior change is safety. Destructive or overwrite operations now ask
for confirmation in interactive mode. For scripts and automation, add `--yes`,
`-y`, or `--force`:

```ohsh
delete temp.txt --yes
copy notes.txt to Backup --force
move draft.txt to final.txt -y
```

Scripts can now be run directly:

```sh
ohsh run setup.osh
ohsh run cleanup.osh --yes
```

Custom aliases and defaults can live in `~/.ohshrc` or a project-local
`.ohshrc`:

```ini
confirm = true
tips = false
fallback_shell = /bin/bash
alias docs = goto Documents
alias files = show files
```
