#!/bin/sh
# one-liner install for git-agent
# Auto-detects platform, downloads release binary, and installs.
#
# Usage:
#   curl -sL https://raw.githubusercontent.com/dutixlf/git-ag/master/install.sh | sh
#   curl -sL ... | PREFIX=$HOME/.local sh
#   curl -sL ... | PREFIX=/usr/local sudo sh
#   curl -sL ... | REPLACE_SYSTEM=1 sudo sh
#
# Or build from source:
#   curl -sL ... | BUILD_FROM_SOURCE=1 sh

set -e

REPO_URL="${REPO_URL:-https://github.com/dutixlf/git-ag}"
PREFIX="${PREFIX:-$HOME/.local}"
REPLACE_SYSTEM="${REPLACE_SYSTEM:-}"
BUILD_FROM_SOURCE="${BUILD_FROM_SOURCE:-}"

detect_os() {
	case "$(uname -s)" in
	Linux*)     echo linux;;
	Darwin*)    echo macos;;
	MINGW*|MSYS*|CYGWIN*) echo windows;;
	*)          echo unknown;;
	esac
}

detect_arch() {
	case "$(uname -m)" in
	x86_64|amd64) echo x86_64;;
	aarch64|arm64) echo arm64;;
	*)          echo unknown;;
	esac
}

OS=$(detect_os)
ARCH=$(detect_arch)

if [ "$OS" = "unknown" ] || [ "$ARCH" = "unknown" ]; then
	echo "Unsupported platform: $(uname -s) $(uname -m)"
	echo "Falling back to build from source..."
	BUILD_FROM_SOURCE=1
fi

if [ -n "$BUILD_FROM_SOURCE" ]; then
	echo "=== Building from source ==="
	TMPDIR=$(mktemp -d)
	trap 'rm -rf "$TMPDIR"' EXIT
	git clone --depth 1 "$REPO_URL.git" "$TMPDIR/git-agent"
	cd "$TMPDIR/git-agent"
	make -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)" \
		NO_CURL=YesPlease NO_RUST=YesPlease
	make prefix="$PREFIX" install
	echo "=== Installed to $PREFIX ==="
	exit 0
fi

# Try to download latest release
echo "=== Detected: $OS $ARCH ==="
LATEST="$REPO_URL/releases/latest"
ASSET="git-agent-${OS}-${ARCH}.tar.gz"

echo "=== Downloading $ASSET ==="
TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

# Try GitHub API first, then fallback to known URL
DOWNLOAD_URL="${REPO_URL}/releases/download/latest/${ASSET}"
if ! curl -fsL -o "$TMPDIR/${ASSET}" "$DOWNLOAD_URL" 2>/dev/null; then
	# Try to find actual tag from redirect
	TAG=$(curl -sL "$LATEST" | sed -n 's|.*tag/\([^"]*\)".*|\1|p' | head -1)
	if [ -z "$TAG" ]; then
		echo "No release found. Falling back to build from source."
		BUILD_FROM_SOURCE=1
		exec "$0" "$@"
	fi
	DOWNLOAD_URL="${REPO_URL}/releases/download/${TAG}/${ASSET}"
	curl -fsL -o "$TMPDIR/${ASSET}" "$DOWNLOAD_URL"
fi

echo "=== Extracting to $PREFIX ==="
mkdir -p "$PREFIX"
tar xzf "$TMPDIR/${ASSET}" -C "$PREFIX" --strip-components=1 2>/dev/null || \
	tar xzf "$TMPDIR/${ASSET}" -C "$PREFIX"

if [ -n "$REPLACE_SYSTEM" ]; then
	echo "=== Replacing system git ==="
	if [ -w /usr/bin ]; then
		cp "$PREFIX/bin/git" /usr/bin/git-agent-binary
		mv /usr/bin/git /usr/bin/git-original
		mv /usr/bin/git-agent-binary /usr/bin/git
		cp "$PREFIX/bin/git" /usr/local/bin/git 2>/dev/null || true
	else
		echo "ERROR: REPLACE_SYSTEM needs write access to /usr/bin"
		echo "Run with sudo or set PREFIX=/usr/local instead"
		exit 1
	fi
fi

echo "=== Done ==="
echo "Installed to $PREFIX"
echo "Add to PATH: export PATH=\"$PREFIX/bin:\$PATH\""
