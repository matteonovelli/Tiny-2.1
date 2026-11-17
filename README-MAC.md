# TINY 2.1 on a Mac

A port, nothing more: TINY 2.1 is the work of
[WollyMonjac](https://github.com/WollyMonjac), original repository
[WollyMonjac/Tiny-2.1](https://github.com/WollyMonjac/Tiny-2.1). This
port only makes the original DOS program, neon screen and PC-speaker
voice included, build natively on macOS (Apple Silicon or Intel).
`TINY.C` and `TINY.BIN` are unchanged; the `mac/` folder stands in for
the Turbo C 2.0 headers TINY uses.

## Build and run

Needs the Xcode command line tools (`xcode-select --install`).

```sh
git clone https://github.com/matteonovelli/Tiny-2.1.git
cd Tiny-2.1
./build.sh      # the Mac BUILD.BAT: makes ./tiny
./tiny
```

Or double-click `TINY.command` in Finder: it builds on first use and opens
TINY in Terminal. Keep `TINY.BIN` next to `tiny`.

Everything in `README.TXT` applies: `./tiny 0`, `./tiny /demo`,
`./tiny /quiet`, `./tiny /mono`, `./tiny /classic`, `./tiny /p`, and
`./tiny < QUESTIONS.TXT > ANSWERS.TXT` for plain text.

## Mac notes

- **F1-F4**: on a Mac keyboard press **fn** with them (fn+F1 help, fn+F2
  sound, fn+F3 voice, fn+F4 creativity), unless the F keys are set to work
  as standard function keys. Delete works as Backspace.
- **Window size**: TINY draws 80 x 25. A new Terminal window is 80 x 24, so
  TINY asks it to grow by one line (and shrinks it back on exit). If the
  terminal does not allow that, TINY asks you to enlarge the window. Larger
  windows are fine: the screen is centred.
- **Colours**: 24-bit colour when the terminal says it has it (`COLORTERM`,
  as iTerm2 and Ghostty do), otherwise the nearest of 256 colours. Force it
  with `TINY_COLORS=24bit ./tiny` or `TINY_COLORS=256 ./tiny`.
- **Sound** goes to the Mac's sound output instead of the PC speaker.
- **Speed**: a Mac answers in a fraction of a second. TINY measures time in
  tenths of a second, so SPEED can show 0.0 for short answers.

## In the browser

`index.html` runs the same network in JavaScript, with the neon screen,
the robot voice and a **letter x-ray**: point at any letter of an answer
to see the other letters TINY weighed, and where it was guessing. It
gives the same answers as the C program at creativity 0.

Play it at [matteonovelli.github.io/Tiny-2.1](https://matteonovelli.github.io/Tiny-2.1/),
or locally: `python3 -m http.server` in this folder, then open
http://localhost:8000 (browsers block loading `TINY.BIN` from a file
opened straight from disk).

## How the port works

| File | Stands in for |
|---|---|
| `mac/conio.h` | Turbo C screen and keyboard functions |
| `mac/dos.h` | `delay`, `sound`/`nosound`, `gettime`, `int86` |
| `mac/io.h` | `isatty` |
| `mac/prelude.h` | read before `TINY.C`: defines `__TURBOC__`, makes `clock()` count real time as in DOS |
| `mac/tcmac.c` | all of the above for a macOS terminal |

- The 80 x 25 text screen is kept as DOS character/colour cells and sent to
  the terminal as UTF-8 (code page 437 box drawing) with ANSI colours.
- The video BIOS calls TINY makes through `int86` are emulated: VGA
  detection, cursor on/off and the colour registers, so its neon palette
  shows as it would on VGA.
- PC-speaker tones are time-stamped and played as a square wave through
  Core Audio about 50 ms later, which keeps the 1-2 ms pitch steps of
  the robot voice.
- `build.sh` compiles `TINY.C` with `-x c` (clang would take the capital
  `.C` for C++).
