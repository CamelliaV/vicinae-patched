#!/bin/bash
# Check for upstream vicinae updates and rebuild with patches

set -e
cd "$(dirname "$0")"

CURRENT_VER=$(grep '^pkgver=' PKGBUILD | cut -d= -f2)
CURRENT_COMMIT=$(grep '^_upstream_commit=' PKGBUILD | cut -d= -f2)
LATEST_VER=$(curl -s https://api.github.com/repos/vicinaehq/vicinae/releases/latest | grep '"tag_name"' | sed 's/.*"v\([^\"]*\)".*/\1/')
LATEST_TAG="v${LATEST_VER}"
LATEST_COMMIT=$(curl -s "https://api.github.com/repos/vicinaehq/vicinae/commits/${LATEST_TAG}" | grep '"sha"' | head -n1 | sed 's/.*"sha": "\(.*\)".*/\1/')

if [[ -z "$LATEST_VER" || -z "$LATEST_COMMIT" ]]; then
    echo "Failed to fetch latest version/commit"
    exit 1
fi

LATEST_SHORT="${LATEST_COMMIT:0:8}"

echo "Current: $CURRENT_VER"
echo "Latest:  $LATEST_VER"
echo "Current commit: $CURRENT_COMMIT"
echo "Latest commit:  $LATEST_COMMIT"

if [[ "$CURRENT_VER" == "$LATEST_VER" && "$CURRENT_COMMIT" == "$LATEST_COMMIT" ]]; then
    echo "Already up to date"
    exit 0
fi

echo "Updating PKGBUILD to $LATEST_VER (${LATEST_SHORT})..."
sed -i "s/^pkgver=.*/pkgver=$LATEST_VER/" PKGBUILD
sed -i "s/^_upstream_commit=.*/_upstream_commit=$LATEST_COMMIT/" PKGBUILD
sed -i "s/^_upstream_short=.*/_upstream_short=$LATEST_SHORT/" PKGBUILD

# Build in a temporary directory so tracked project files are never removed
TMP_BUILD_DIR=$(mktemp -d)
trap 'rm -rf "$TMP_BUILD_DIR"' EXIT
cp PKGBUILD vicinae-enhancements.patch "$TMP_BUILD_DIR"/

echo "Building..."
(cd "$TMP_BUILD_DIR" && makepkg -f)

PKG_FILE=$(ls "$TMP_BUILD_DIR"/vicinae-patched-*.pkg.tar.* | head -n1)
cp "$PKG_FILE" .

echo "Install with: sudo pacman -U $(basename "$PKG_FILE")"
