#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"

if ! command -v brew >/dev/null 2>&1; then
  echo "Homebrew is required to verify the formula."
  exit 1
fi

tap_dir="$(brew --repository gabex47/tap 2>/dev/null || true)"
if [ -z "$tap_dir" ]; then
  brew tap-new gabex47/tap
  tap_dir="$(brew --repository gabex47/tap)"
fi

mkdir -p "$tap_dir/Formula" "$tap_dir/scripts"
cp "$repo_root/homebrew-tap/Formula/ohsh.rb" "$tap_dir/Formula/ohsh.rb"
cp "$repo_root/homebrew-tap/README.md" "$tap_dir/README.md"
cp "$repo_root/homebrew-tap/scripts/update-stable-formula.sh" "$tap_dir/scripts/update-stable-formula.sh"

brew audit --strict --formula gabex47/tap/ohsh

if brew list --formula ohsh >/dev/null 2>&1; then
  if [ "${OHSH_HOMEBREW_REINSTALL:-0}" != "1" ]; then
    echo "ohsh is already installed with Homebrew."
    echo "Set OHSH_HOMEBREW_REINSTALL=1 to uninstall it and verify a fresh HEAD install."
    exit 0
  fi
  brew uninstall ohsh
fi

brew install --HEAD --build-from-source gabex47/tap/ohsh
brew test gabex47/tap/ohsh
