> **macOS port.** This fork only makes [TINY 2.1](https://github.com/WollyMonjac/Tiny-2.1)
> by [WollyMonjac](https://github.com/WollyMonjac) build and run on a Mac.
> TINY, its network and all of the original code are their work, and
> `TINY.C` is unchanged here: the port adds `build.sh` and the `mac/`
> folder. See [README-MAC.md](README-MAC.md) to run it on a Mac.
> The original README follows.

# TINY 2.1 — a transformer for DOS

![TINY 2.1 running under DOS](tiny21.png)

A tiny question-answering transformer written in Turbo C 2.0. It runs on a
Pentium or 486DX under plain DOS and fits on a floppy disk.

- 367,000 parameters: 4 layers, 96 wide, reads and writes letter by letter
- 8-bit weights (one scale per row), 380 KB in all
- Needs about 540 KB of free conventional memory
- Trained on about 3,500 facts, each asked in many wordings
  Outside those it makes things up confidently
- Neon full-screen interface with a live neural monitor and a PC-speaker
  robot voice

Written by Fable 5.2 and trained by Opus 5.5.

## Run

```
C:\> CD \TINY
C:\TINY> TINY
```

Keep `TINY.EXE` and `TINY.BIN` in the same folder. To rebuild:
`TCC -ml TINY.C`
