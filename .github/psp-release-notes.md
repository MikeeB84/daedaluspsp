DaedalusX64 for the PSP: current DaedalusX64 with crash fixes, a benchmarked compatibility list, cleaner audio and tools for reporting problems.

## Install

1. Unzip and copy the `DaedalusX64` folder to `PSP/GAME` on your Memory Stick. If you already have DaedalusX64 there, back up your `SaveGames` folder first.
2. Put your ROMs in `PSP/GAME/DaedalusX64/Roms`.
3. A PSP 2000 or later is recommended.

The `Benchmark` folder is optional: it runs every ROM by itself and logs how each one runs (see `HOW-TO-BENCHMARK.txt`).

## Compatibility list

Every game in the ROM list has a coloured square: green runs at 80% of N64 speed or better, yellow runs but slower, red doesn't start or stops. 933 entries are rated; 915 come from running every ROM of a full collection on a PSP 3000 with this build (30 seconds to see what starts, then 90 seconds for speed).

From that run, of 1,176 game ROMs: 842 start and keep running, 330 at full N64 speed and 501 at 80% or better. A short automatic run can't tell whether a game is playable all the way through, so reports from real play are welcome.

## Fixes

- Dynarec crash in looping code: 007 The World Is Not Enough (and likely other games) crashed after a few minutes; it now plays through.
- Triangle clipping table aligned for the PSP's vector unit.
- Code cache is also cleared when its buffers are nearly full.
- ROMs over 16 MB in ROM Buffer mode, and short reads in File Cache mode, no longer break loading.
- Per-game switches in `roms.ini` turn off shortcuts for games that crash with them (Bomberman 64: The Second Attack and Baku Bomberman 2 now run).

## Audio

- Less crackling, and no "underwater" pitch wobble when a game runs slow.
- Asynchronous audio no longer corrupts games or runs two audio jobs at once.

## Bug reports

- Crash screen and `exception.txt`, `hang.txt` when a game stops showing frames, and an FPS + Timing display (Settings, Display Info) that writes `perf.txt`. Attach these when reporting a slow or broken game.

## Known problems

- Vigilante 8 and Vigilante 8: 2nd Offense switch the PSP off.
- Excitebike 64 can lock up.
- Some games freeze after a minute or two (for example Castlevania, Charlie Blast's Territory).

Based on DaedalusX64 by the DaedalusX64 team: https://github.com/DaedalusX64/daedalus
