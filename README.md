# DaedalusX64 (PSP)

DaedalusX64 is a Nintendo 64 emulator for the Sony PSP.

This fork runs on the PSP only. Support for 3DS, Vita, Linux, macOS and Windows has been removed, so all the work goes into making N64 games run faster and more reliably on PSP hardware.

## Features

- Dynamic recompiler (dynarec) that translates N64 MIPS code into native PSP code
- VFPU-accelerated graphics maths and optional audio processing on the Media Engine
- High-level emulation of graphics and audio microcode
- Compatibility rating shown as a green, yellow or red square next to each game in the ROM list (see [Compatibility ratings](#compatibility-ratings))
- Built-in timing display and performance logs to see where each game spends its time (see [Performance display and logs](#performance-display-and-logs))
- Crash screen that saves `exception.txt`, and a watchdog that writes `hang.txt` when a game freezes, for bug reports

## Changes in this fork

### PSP-only codebase

- All non-PSP platforms and their code paths have been removed. CMake refuses to configure for anything other than the PSP.
- `build_daedalus.sh` always builds for the PSP. CI builds the PSP version only.
- The EBOOT requests the full memory of the PSP 2000 and later (`MEMSIZE 1`). Newer toolchains otherwise only give 24 MB, and the emulator dropped back to the XMB at start-up.

### Speed

- **Dynarec:** code reached after interrupts and exceptions, and code reached when compiled code leaves by a path it did not take when it was recorded, is now counted and compiled. Previously both stayed in the interpreter. Hot code whose recording was interrupted is retried instead of being abandoned.
- **Dynarec:** the upper 32 bits of N64 registers are only written out when something needs them. Most N64 code uses 32-bit values, and the generated code used to store the upper half after nearly every instruction.
- **Dynarec:** the table that counts how often each branch target is hit is a flat hash table instead of a `std::map`.
- **CPU:** recent TLB translations are cached, which helps games that run from TLB-mapped memory (GoldenEye, Perfect Dark, Conker...).
- **CPU:** a lock was removed from the per-instruction event check.
- **Graphics:** the last blend state lookup is remembered, and the renderer avoids copying `shared_ptr`s in hot paths.
- A fast `FastRand()` replaces creating a new random engine on every call.

### Audio

- **Crackling removed.** The audio buffer held only 2048 samples while the PSP audio thread takes 1024 at a time, so any slow frame ran it dry and the output snapped to silence (a click) and back. The rate converter also restarted on every audio chunk, causing a small glitch every 16-30 ms even at full speed. The buffer is now 4096 samples, the rate converter runs continuously, and pitch is always exact.
- **Soft gaps when a game runs slow.** If a game cannot produce audio fast enough, playback fades out and waits until the buffer is half full before fading back in, instead of crackling. At full speed the audio is clean.
- **Asynchronous audio no longer corrupts games.** The Media Engine wrote its results to N64 RAM through its own cache, which is written back in 64-byte blocks and could overwrite game data next to the audio buffers (for example Super Mario 64 crashing with sound on). It now writes to N64 RAM directly.
- **Asynchronous audio never runs two audio lists at once.** The main CPU used to process the next list itself while the Media Engine was still busy, corrupting both. It now waits for the Media Engine, and falls back to synchronous audio if the Media Engine stops responding.

### Fixes

- **Dynarec:** `MTLO`/`MTHI` read the wrong register in compiled code, which set the multiply result registers to zero.
- **File Cache mode (the default):** a ROM read that came up short (the last chunk of a ROM whose size is not a multiple of 8 KB) left the file stream in an error state, so every later read failed and the game was fed stale data. The error state is now cleared before every read.
- **Loads past the end of the ROM** copy what exists instead of nothing.
- **ROM Buffer mode:** ROMs between 16 MB and 32 MB were loaded into a 16 MB buffer on PSP 2000 and later, which crashed. Larger ROMs now stream through the file cache.
- **Crash screen:** it was only enabled in debug builds and looked for its plugin in the wrong folder. It now works in release builds. `exception.txt` includes PSP and N64 disassembly around the crash and what the emulator was doing at the time.
- **Per-game settings in `roms.ini`** can now turn features off. `PatchesEnabled=no` (the High Level Emulation setting), `DynarecLoopOptimisation=no` and `MemoryAccessOptimisation=no` force those off for a game known to break with them. Previously `roms.ini` could only turn things on. Cruis'n USA uses this and is now playable.

### Compatibility ratings

`roms.ini` entries can carry `Compatibility=Good`, `Partial` or `Broken`, shown in the ROM list as a square before the game's name and as a "Compat:" line in the info panel:

- Green (`Good`): playable or better.
- Yellow (`Partial`): runs, but too slow or glitchy to really play.
- Red (`Broken`): crashes or hangs.
- No square: not rated yet.

Ratings are matched by the ROM's ID, so they work whatever the file is called. They come from the Daedalus PSP compatibility list (tested with DaedalusX64 R1878), the upstream wiki and testing of this fork on real hardware. Some red ratings predate fixes in this fork, so a game marked red may now work: reports are welcome.

### Performance display and logs

Set **Display Framerate** to **FPS + Timing** in the global settings to show a breakdown under the FPS counter:

- `CPU: INT DYN JIT OTH`: time spent interpreting, running compiled code, compiling, and other CPU work.
- `GFX: DL VTX TEX DRAW`: display list parsing, vertex processing, texture handling and drawing.

While this is on, the emulator also writes these files to the DaedalusX64 folder:

- `perf.txt`: one line per second with the breakdown above, plus audio time, GPU wait and idle time.
- `dynarec.txt`: every ~10 seconds, a report of the hottest interpreted and compiled code, traces that failed to compile and why, fragment cache flushes, and the game's ROM loads (count, failures, last addresses).

Two more files are written when something goes wrong, whatever the setting:

- `exception.txt`: saved from the crash screen (press X). Registers, disassembly and what the emulator was doing when it crashed.
- `hang.txt`: written if a game stops showing new frames for about 4 seconds. Records what the emulator was stuck on, the N64 code position, the graphics microcode and the last graphics command.

These files are the most useful thing to attach when reporting a slow or broken game.

## Known issues

- Duke Nukem: Zero Hour stays on its loading screen when starting the main game. The game keeps running (30 fps, streaming from the ROM) but never finishes loading. This is under investigation.
- Star Wars Episode I: Racer runs but slowly (about 7-12 fps in races).
- GoldenEye 007 does not get past the intro.

## Usage

Copy the entire `DaedalusX64` folder to your Memory Stick, into `PSP/GAME`, and put your ROMs in `DaedalusX64/Roms`.

A PSP 2000 or later is recommended: it has enough memory for the larger ROM buffer and texture cache.

## Building

Fetch the latest PSP toolchain from https://github.com/pspdev/pspdev and make sure its `bin` folder is on your `PATH` and `PSPDEV` is set.

    ./build_daedalus.sh          # Release build
    ./build_daedalus.sh DEBUG    # Debug build (asserts, debug console, logging)

The finished build is placed in the `DaedalusX64` folder.

## More Info

This fork is based on the upstream DaedalusX64 project: https://github.com/DaedalusX64/daedalus

For compatibility lists and optimal settings, see the upstream wiki: https://github.com/DaedalusX64/daedalus/wiki

Upstream Discord: https://discord.gg/FrVTpBV

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
