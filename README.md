# DaedalusX64 (PSP)

DaedalusX64 is a Nintendo 64 emulator for the Sony PSP (and, upstream, 3DS, Vita, Linux, macOS and Windows).

This fork is the current DaedalusX64 (https://github.com/DaedalusX64/daedalus) with fixes, compatibility ratings, cleaner audio and tools for reporting problems, aimed at the PSP. Nothing here changes how games are emulated except the bug fixes listed below.

## Speed

On a PSP 3000, current DaedalusX64 was measured against the old 1.1.8 release with the benchmark mode below: 62 games tested on both, 37 ran in both.

- Average 35.6 fps here against 31.3 fps on 1.1.8, with more spare PSP time (27% against 10%).
- Faster in 20 of those games, slower in 8, about the same in 9.
- 1.1.8 is still faster in a few games (Army Men: Sarge's Heroes, Deadly Arts, Fighting Force 64, Duke Nukem: Zero Hour).

## What this fork adds

### Fixes

- **Dynarec loop crash.** With the loop optimisation (on by default), code looping back to its own start branched straight back to the top. When that branch was written in the second code buffer, the top was out of range for a PSP branch instruction and the jump landed in unused code: the PSP ran garbage, overwrote emulator variables and crashed. 007: The World Is Not Enough crashed this way after about 3.5 minutes every time and now plays through.
- **Clipping alignment.** The table used to clip triangles is aligned to 16 bytes again, which the PSP's vector unit needs. Without it, unrelated changes could make every game crash when clipping.
- **Code buffer safety.** The compiled code cache is also cleared when the code buffers are nearly full, not only after a fixed number of compiled blocks (release builds had no check at all).
- **ROMs over 16MB in ROM Buffer mode** no longer crash: they use the file cache when they don't fit.
- **File Cache mode:** a short read at the end of a ROM no longer breaks every later read.
- (The `MTLO`/`MTHI` dynarec fix is now part of upstream.)

### Compatibility ratings

`roms.ini` entries can carry `Compatibility=Good`, `Partial` or `Broken`, shown in the ROM list as a coloured square before the game's name and as a "Compat:" line in the info panel:

- Green (`Good`): runs at 80% of N64 speed or better.
- Yellow (`Partial`): runs, but below 80% of N64 speed.
- Red (`Broken`): doesn't start, or freezes, crashes or locks up.
- No square: not rated yet.

933 entries are rated. 915 of them come from benchmarking every ROM of a full N64 collection on a PSP 3000 with this build: a 30 second run of each to see what starts, then a 90 second run of the ones that did, measuring N64 speed over the last 60 seconds (Start and A are pressed during the first 30 to get past title screens). The rest keep their rating from the Daedalus PSP compatibility list and the upstream wiki. A short automatic run can't tell whether a game is playable all the way through, so reports from real play are welcome.

`roms.ini` can also turn features off for games known to break with them: `PatchesEnabled=no`, `DynarecLoopOptimisation=no` and `MemoryAccessOptimisation=no` force those off for that game, whatever the player's settings. They are used for:

- Cruis'n USA: all three off.
- All-Star Baseball 99 / 2000 / 2001, Bomberman 64: The Second Attack! / Baku Bomberman 2 and Battlezone: memory optimisation off. It removes the address check from loads and stores that hit N64 RAM while the code was being compiled; these games later use the same code with other addresses (null pointers, TLB-mapped memory) and crashed. Both Bomberman games now run; the others no longer crash but still have other problems.

### Audio

- **Less crackling.** The audio buffer holds 4096 samples (the PSP plays 1024 at a time), and when a game can't produce audio fast enough playback fades out and waits until the buffer is half full instead of clicking.
- **No "underwater" pitch wobble.** Pitch is always exact.
- **Asynchronous audio no longer corrupts games.** The Media Engine (the PSP's second CPU) wrote N64 memory through its own cache, which could overwrite game data next to the audio buffers (for example Super Mario 64 crashing with sound on). It now writes N64 memory directly.
- **Asynchronous audio never runs two audio lists at once.** The main CPU waits for the Media Engine, and switches to synchronous audio if it stops responding.

### Bug reports

- **Crash screen and `exception.txt`** in release builds: registers, the code around the crash, what the emulator was doing, and where the compiled code buffers are.
- **`hang.txt`** is written if a game stops showing new frames for about 4 seconds.
- **FPS + Timing.** Set **Display Info** to **FPS + Timing** to show a time breakdown under the FPS counter (`CPU: INT DYN JIT OTH`, `GFX: DL VTX TEX DRAW`). While it is on, the emulator also writes `perf.txt` (one line per second) and `dynarec.txt` (every ~10 seconds). With any other Display Info setting the timing code does nothing.

These files are the most useful thing to attach when reporting a slow or broken game.

### Benchmark mode

If a file called `benchmark.ini` is in the DaedalusX64 folder, Daedalus runs every ROM in turn instead of showing the menu (5 minutes each by default) and writes one line per game to `benchmark.txt`: result (OK, froze, no picture, crashed), average FPS, slowest and fastest 10-second stretch, N64 speed, spare PSP time and how busy the CPU and graphics were. Second-by-second details go to `perf.txt`.

- Start and A are pressed every few seconds during the first minute to get past title screens; the figures are measured after that.
- A game that freezes or never shows a picture is cut short. After a crash it saves `exception.txt` and restarts.
- Progress is kept in `benchmark_progress.txt`. After a crash or switching the PSP off, starting it again carries on; a ROM that locks up the PSP is run once more, then skipped.
- `Only=` lines limit the run to ROMs whose file name contains that text.
- **Two passes:** with `SecondPassSeconds` set, every ROM first gets a short run (`Seconds`). When that is finished, its results are kept as `benchmark_pass1.txt` / `perf_pass1.txt`, the ROMs that ran OK (showed a picture and kept running without crashing or freezing) are listed in `benchmark_pass2_list.txt`, and just those are run again for `SecondPassSeconds`. For example 30 seconds to see what loads, then 90 seconds for speed figures.

The benchmark is meant to be installed as a separate folder (for example `PSP/GAME/DaedalusX64Bench`) reading the ROMs from the normal install. Do not put `benchmark.ini` in your normal DaedalusX64 folder.

## Usage

Copy the entire `DaedalusX64` folder to your Memory Stick, into `PSP/GAME`, and put your ROMs in `DaedalusX64/Roms`.

A PSP 2000 or later is recommended.

## Building
All Versions require these libraries: zlib, libpng, minizip
 ## Building for PSP
    Fetch the latest PSP Toolchain from https://github.com/pspdev/pspdev and install required libraries
    
    Run    ./build_daedalus.sh PSP and CMake will do the required fun stuff :)

## Building for Windows 

NB: Windows build is broken at the moment 

1) Clone and open the repo in Visual studio 2019

2) Build All

## Building for Posix OS (Linux & macOS)
Posix OSes additionally require glew / SDL2

1) Clone this repo 

2) ./build_daedalus.sh (This will auto detect the OS you are on)
 
 ## CI
 DaedalusX64 now has CI, you can get the latest nightlies from the Actions Tab.
 Warning: these builds are sporatic at times.

## More Info
 
For information about compatibility, optimal settings and more about the emulator, visit the actively maintained GitHub wiki page: https://github.com/DaedalusX64/daedalus/wiki Feel free to submit reports for how well your favourite games run if they have not already been listed!
 
We're on Discord, come catch up with the latest news. Chaos ensured. https://discord.gg/FrVTpBV
 
## Credits
- StrmnNrmn - For bringing this project to us
- Kreationz, Howard0Su salvy, Corn, Chilly Willy, Azimer, Shinydude100 and missed folks for past contributions
- MasterFeizz for 3DS Support / ARM DynaRec
- Xerpi / Rinnegatamante / TheOfficialFloW for PS Vita and ARM Contributions
- z2442 & Wally: Compilation improvements and updating, optimizations
- TheMrIron2 & mrneo240 Optimizations, wiki maintenance
- re4thewin for continuous testing over the duration of DaedalusX64
- motolegacy for help with site and emulator itself
- fjtrujy for help with getting CI going.
- Our Valued Communities for helping making Daedalus what it is :)
