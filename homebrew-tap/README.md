# Homebrew Tap for OHSH

This repository is the Homebrew tap for
[OHSH](https://github.com/gabex47/ohsh), a human-first shell language.

## Install

Install the latest development build from the `main` branch:

```sh
brew install --HEAD gabex47/tap/ohsh
```

Or tap the repository first:

```sh
brew tap gabex47/tap
brew install --HEAD ohsh
```

## Update

For a HEAD install:

```sh
brew update
brew reinstall --HEAD gabex47/tap/ohsh
```

When a stable formula URL is added after a tagged release:

```sh
brew update
brew upgrade ohsh
```

## Uninstall

```sh
brew uninstall ohsh
```

To remove the tap:

```sh
brew untap gabex47/tap
```

## Local Validation

From this tap repository:

```sh
brew audit --strict --formula gabex47/tap/ohsh
brew install --HEAD --build-from-source gabex47/tap/ohsh
brew test gabex47/tap/ohsh
```

If you are validating before the tap repository exists on GitHub, run
`brew tap-new gabex47/tap`, copy this repository's files into that local tap
checkout, and then run the commands above.

## Stable Releases

The current formula supports `--HEAD` installs from `main`.

For stable installs, publish a tagged OHSH release first, then run:

```sh
./scripts/update-stable-formula.sh v0.3.0
```

Commit the updated `Formula/ohsh.rb` and push it to this tap.
