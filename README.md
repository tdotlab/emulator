# Nacre

A compact CHIP-8 interpreter in C, with a terminal frontend and a plain SDL3 desktop prototype. Both frontends use the same emulator core and sound oscillator.

![IBM Logo](screenshots/ibm-logo.png)
*IBM Logo — the classic first test.*

![Tetris](screenshots/tetris.png)
*Tetris — a piece falling into the well.*

![Breakout](screenshots/breakout.png)
*Breakout — bricks and paddle, waiting for serve.*

These images show the emulator's framebuffer as PNGs.

## Build and play

You need a C99 compiler, `make`, and SDL2 development files for live audio. `pkg-config` is used to find SDL2.

```sh
make
./play path/to/game.ch8
# Or run a specific ROM directly:
./nacre path/to/game.ch8 --play
```

The live sound is a smooth 440 Hz tone that follows the CHIP-8 sound timer. The `m` key mutes and unmutes it. Use `--sound off` for a silent play session. SDL2 is only needed for live audio; without it, the program still builds for headless use and WAV capture.

The terminal uses half-block characters by default to fit the display into 64×16 cells. Use `--render full` for double-width blocks. In play mode, use lowercase letters for the standard CHIP-8 key map; uppercase `R` resets the game:

```text
1 2 3 4        1 2 3 C
q w e r   →    4 5 6 D
a s d f        7 8 9 E
z x c v        A 0 B F
```

| Key | Action |
| --- | --- |
| `p` | Pause or resume |
| `R` | Reset the ROM |
| `+` / `-` | Increase or decrease speed |
| `m` | Mute or unmute sound |
| `Esc` | Quit |

## Desktop prototype

The desktop app uses the same C core and SDL3 frontend on macOS, Windows, and Linux. Install CMake 3.20 or later and a C compiler. CMake packages the demo ROM with each build and links SDL3 statically, so the app does not need a separate SDL installation. The first configure downloads the pinned SDL3 source archive. The SDL license is included with each build.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The macOS build is `build/Nacre.app`. Windows and Linux produce a `Nacre` executable alongside `assets/hello.ch8` and `THIRD_PARTY_NOTICES.txt`. Launching the app opens the bundled demo; use these controls:

| Key | Action |
| --- | --- |
| `1`–`4`, `Q/W/E/R`, `A/S/D/F`, `Z/X/C/V` | CHIP-8 keypad |
| `Space` | Pause or resume |
| `F2` | Reset the current ROM |
| `M` | Mute or unmute |
| `Cmd+O` / `Ctrl+O` | Open a ROM |
| `Cmd+Q` / `Ctrl+Q`, `Esc` | Quit |

The [Desktop builds workflow](https://github.com/tdotlab/nacre/actions/workflows/desktop.yml) uploads unsigned CI artifacts for each OS. Its macOS artifact is a universal `Nacre.app` for Apple Silicon and Intel. CI confirms that the apps compile; native launch, input, and audio checks are still pending.

The window keeps the CHIP-8 display's 2:1 aspect ratio and uses nearest-neighbor scaling. The desktop sound output uses SDL3 and the same oscillator as the terminal and WAV backends. macOS, Windows, and Linux builds are separate because each OS needs its own executable and packaging; the emulator core and desktop source are shared. Linux may need the SDL development packages listed in `.github/workflows/desktop.yml` to build window and audio drivers.

## Interpreter behavior

Nacre implements the classic CHIP-8 instruction set with these choices: `8xy6` and
`8xyE` shift `Vx`; `Fx55` and `Fx65` leave `I` unchanged; `Bnnn` adds `V0`; and
sprites wrap at screen edges. `Fx0A` waits for a new key press and its release.
SUPER-CHIP and XO-CHIP are not implemented. Legacy `0nnn` system calls are ignored.
Seven classic Timendus ROMs ran for 20,000 simulated cycles without an emulator
error. This was a crash smoke check; their displayed results have not been reviewed.

The default speed is 10 instructions per 60 Hz virtual frame (600 instructions per
second). `--speed N` changes CPU speed without changing timer or display rates.

## ROMs

`hello.ch8` draws a small demo. `sound.ch8` sets the buzzer timer and loops, so it is a quick way to hear the sound output.

Pass any compatible `.ch8` file to `./play` or `./nacre`. To download the optional game collection for the terminal picker's local library, run:

```sh
./get-roms.sh
./play
```

The collection is cloned into `.roms-cache/`; `roms/` is a local symlink to its `games/` directory. Both paths are ignored by Git. The ROM collection comes from [kripod/chip8-roms](https://github.com/kripod/chip8-roms).

## Headless use

```sh
./nacre hello.ch8 --cycles 50
./nacre path/to/rom.ch8 --cycles 2000 --dump framebuffer.txt
./nacre sound.ch8 --cycles 300 --sound wav --sound-file sound.wav
./nacre --selftest
```

Headless mode runs 2,000 CPU cycles by default at 600 cycles per virtual second and
prints the 64×32 framebuffer as `#` and `.`. `--dump PATH` writes it to a file;
`--dump -` writes to standard output. `--sound wav` records a mono 48 kHz WAV,
including silence; CHIP-8 timers tick at 60 Hz, so 600 cycles produce one simulated
second. The default output path is `nacre.wav`.

`make clean` removes the compiled executable. The ROMs and generated WAV files are not tracked by Git.

## Development

Run `make test` for the SDL-free scheduler and key-wait checks. The desktop source,
cross-platform build, and remaining Mac acceptance work are tracked in
[the roadmap](docs/ROADMAP.md).
