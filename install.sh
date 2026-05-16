#!/bin/sh
# one-liner install for git-agent
# Usage: curl -sL https://raw.githubusercontent.com/OWNER/REPO/master/install.sh | sh

set -e

REPO_URL="${REPO_URL:-https://github.com/dutixlf/git-ag.git}"
PREFIX="${PREFIX:-$HOME/.local}"

TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

echo "=== cloning git-agent ==="
git clone --depth 1 "$REPO_URL" "$TMPDIR/git-agent"

cd "$TMPDIR/git-agent"

echo "=== building ==="
make -j"$(nproc)" NO_CURL=YesPlease NO_RUST=YesPlease

echo "=== installing to $PREFIX ==="
make prefix="$PREFIX" install

echo "=== done ==="
echo "Add $PREFIX/bin to your PATH if not already there:"
echo "  export PATH=\"$PREFIX/bin:\$PATH\""
