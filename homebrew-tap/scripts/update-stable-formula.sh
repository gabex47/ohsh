#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
  echo "usage: $0 <tag>"
  echo "example: $0 v0.3.0"
  exit 64
fi

tag="$1"
formula_dir="$(cd "$(dirname "$0")/.." && pwd)"
formula="$formula_dir/Formula/ohsh.rb"
url="https://github.com/gabex47/ohsh/archive/refs/tags/${tag}.tar.gz"

if ! command -v curl >/dev/null 2>&1; then
  echo "curl is required"
  exit 1
fi

if ! command -v shasum >/dev/null 2>&1; then
  echo "shasum is required"
  exit 1
fi

sha256="$(curl -L "$url" | shasum -a 256 | awk '{print $1}')"

tmp="$(mktemp)"
awk -v url="$url" -v sha="$sha256" '
  BEGIN { inserted = 0; skip_sha = 0 }
  skip_sha == 1 && /^  sha256 / { skip_sha = 0; next }
  /^  url "https:\/\/github.com\/gabex47\/ohsh\/archive\/refs\/tags\// {
    skip_sha = 1
    next
  }
  /^  head / && inserted == 0 {
    print "  url \"" url "\""
    print "  sha256 \"" sha "\""
    print ""
    inserted = 1
  }
  { print }
' "$formula" > "$tmp"

mv "$tmp" "$formula"

echo "Updated $formula"
echo "url:    $url"
echo "sha256: $sha256"
