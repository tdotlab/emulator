# Project guidance

Nacre is a C CHIP-8 emulator with terminal/headless and SDL3 desktop frontends.
The desktop frontend is shared across macOS, Windows, and Linux; keep OS-specific
packaging outside the emulator core. Keep `chip8_*` and `CHIP8_*` identifiers for
the emulated architecture; use Nacre for product branding.

- Read README.md for current setup, commands, and entry points.
- Read docs/ROADMAP.md for the desktop design, review findings, milestones, and open decisions.
- Keep the emulator core independent of SDL, terminal APIs, and desktop UI code.
- Retain the CLI while adding a desktop frontend. Implement one bounded milestone at a time.
- Test changed opcode, timing, input, and audio behavior with meaningful checks.
  Report compilation, automated checks, and actual device testing separately.
- Keep source in src/, headers in include/, and add tests/docs/assets/tools in their
  conventional directories when needed. Update existing docs when behavior changes.
- Do not claim Mac distribution or live audio validation from a headless smoke run.
- Follow the owner's current authorization for commits and releases. Do not store
  credentials in this repository or redistribute ROMs without verified provenance.
