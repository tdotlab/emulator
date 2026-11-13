#!/bin/sh
# Download or update the CHIP-8 ROM collection used by ./play.
set -eu

DEST="$HOME/workspace/little-mike-space/chip8-roms"
URL="https://github.com/kripod/chip8-roms.git"

if [ -d "$DEST/.git" ]; then
    echo "Updating $DEST"
    git -C "$DEST" pull --ff-only
else
    echo "Cloning $URL -> $DEST"
    git clone --depth 1 "$URL" "$DEST"
fi

games=$(find "$DEST/games" -name '*.ch8' 2>/dev/null | wc -l)
echo "Ready: $games games in $DEST/games"
echo "Run ./play to pick one."
