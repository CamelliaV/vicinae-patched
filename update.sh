#!/bin/bash
# Check for upstream vicinae updates and rebuild with patches

set -e
cd "$(dirname "$0")"

CURRENT_VER=$(grep '^pkgver=' PKGBUILD | cut -d= -f2)
LATEST_VER=$(curl -s https://api.github.com/repos/vicinaehq/vicinae/releases/latest | grep '"tag_name"' | sed 's/.*"v\([^"]*\)".*/\1/')

if [[ -z "$LATEST_VER" ]]; then
    echo "Failed to fetch latest version"
    exit 1
fi

echo "Current: $CURRENT_VER"
echo "Latest:  $LATEST_VER"

if [[ "$CURRENT_VER" == "$LATEST_VER" ]]; then
    echo "Already up to date"
    exit 0
fi

echo "Updating PKGBUILD to $LATEST_VER..."
sed -i "s/^pkgver=.*/pkgver=$LATEST_VER/" PKGBUILD

# Clean old build artifacts
rm -rf src build pkg *.tar.gz *.pkg.tar.zst

echo "Building..."
makepkg -f

echo "Install with: sudo pacman -U vicinae-patched-${LATEST_VER}-1-x86_64.pkg.tar.zst"
