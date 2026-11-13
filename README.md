# CHIP-8 emulator

A small CHIP-8 interpreter written in C, with a terminal display and no external library dependencies. It can run interactively or dump its framebuffer for headless testing.

![IBM Logo](screenshots/ibm-logo.png)
*IBM Logo — the classic first test.*

![Tetris](screenshots/tetris.png)
*Tetris — a piece falling into the well.*

![Breakout](screenshots/breakout.png)
*Breakout — bricks and paddle, waiting for serve.*

These images show the emulator's 64×32 framebuffer, rendered as PNGs.

## Build and play

You need a C99 compiler and `make`:

```sh
make
./play path/to/game.ch8
# Or run the emulator directly:
./chip8 path/to/game.ch8 --play
```

The terminal display uses half-block characters to fit the 64×32 framebuffer into 64×16 character cells. Use `--render full` for double-width blocks, or `--render half` for the default.

CHIP-8 keys map to the keyboard like this:

```text
1 2 3 4        1 2 3 C
Q W E R   →    4 5 6 D
A S D F        7 8 9 E
Z X C V        A 0 B F
```

| Key | Action |
| --- | --- |
| `p` | Pause or resume |
| `R` | Reset the ROM |
| `+` / `-` | Increase or decrease speed |
| `m` | Mute or unmute |
| `Esc` or uppercase `Q` | Quit |

Use `--speed N` to set cycles per frame (default 10, approximately 600 instructions per second at 60 fps). Keys remain held for a few frames after a press, so short holds register.

## ROMs

The repository includes two small test programs: `hello.ch8` draws a demo, and `sound.ch8` exercises the buzzer. You can play any compatible `.ch8` file by passing its path to `./play` or `./chip8`.

`./play` without an argument lists files in a local `roms/` directory. The optional `./get-roms.sh` script clones or updates the [kripod CHIP-8 ROM collection](https://github.com/kripod/chip8-roms) at `$HOME/workspace/little-mike-space/chip8-roms`; it does not create `roms/` here. To use the picker with that collection, create a symlink:

```sh
./get-roms.sh
ln -s "$HOME/workspace/little-mike-space/chip8-roms/games" roms
./play
```

## Headless testing

```sh
./chip8 hello.ch8 --cycles 50
./chip8 path/to/rom.ch8 --cycles 2000 --dump framebuffer.txt
./chip8 --selftest
```

Headless mode runs 2,000 CPU cycles by default and writes the framebuffer as `#` and `.` to standard output. `--dump` selects an output file; `--dump -` selects standard output. A program waiting for a key with `Fx0A` blocks in headless mode.

## Sound

The CHIP-8 buzzer sounds while the sound timer is nonzero. Interactive mode defaults to the terminal bell. Choose `--sound bell`, `--sound off`, or `--sound wav --sound-file out.wav` to write a synthesized 440 Hz square wave. `sound.ch8` sets the timer for 10 ticks and loops.

Run `make clean` to remove the compiled executable.
