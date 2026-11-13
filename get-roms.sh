#!/bin/sh
# Fetch or update the optional CHIP-8 game collection and connect it to ./play.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
DEST=${CHIP8_ROM_CACHE:-"$ROOT/.roms-cache"}
case "$DEST" in
    /*) ;;
    *) DEST="$ROOT/$DEST" ;;
esac
URL=https://github.com/kripod/chip8-roms.git

if [ -d "$DEST/.git" ]; then
    echo "Updating ROM collection in $DEST"
    git -C "$DEST" pull --ff-only
elif [ -e "$DEST" ]; then
    echo "ROM cache path exists but is not a Git checkout: $DEST" >&2
    exit 1
else
    echo "Cloning ROM collection into $DEST"
    git clone --depth 1 "$URL" "$DEST"
fi

GAMES="$DEST/games"
if [ ! -d "$GAMES" ]; then
    echo "ROM collection has no games directory: $GAMES" >&2
    exit 1
fi

if [ -L "$ROOT/roms" ]; then
    if [ "$(readlink "$ROOT/roms")" != "$GAMES" ]; then
        echo "roms is already a symlink to another location; leaving it alone" >&2
        exit 1
    fi
elif [ -e "$ROOT/roms" ]; then
    echo "Using existing roms directory at $ROOT/roms"
else
    ln -s "$GAMES" "$ROOT/roms"
fi

count=$(find "$GAMES" -type f -name '*.ch8' | wc -l | tr -d ' ')
echo "Ready: $count ROMs. Run ./play to choose one."
