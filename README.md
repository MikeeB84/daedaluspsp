# DaedalusX64 (PSP)

DaedalusX64 is a Nintendo 64 emulator for the Sony PSP.

This fork is based on the **DaedalusX64 1.1.8** release, which runs games noticeably faster on real PSP hardware than later versions. On top of 1.1.8 it adds compatibility ratings in the ROM list, cleaner audio, a few bug fixes and tools for reporting problems. Everything else is 1.1.8 as released.

## What this fork adds

### Compatibility ratings

`roms.ini` entries can carry `Compatibility=Good`, `Partial` or `Broken`, shown in the ROM list as a coloured square before the game's name and as a "Compat:" line in the info panel:

- Green (`Good`): playable or better.
- Yellow (`Partial`): runs, but too slow or glitchy to really play.
- Red (`Broken`): crashes or hangs.
- No square: not rated yet.

Ratings are matched by the ROM's ID, so they work whatever the file is called. 350 games are rated, from the Daedalus PSP compatibility list (tested with DaedalusX64 R1878), the upstream wiki and testing on real hardware. Reports are welcome, especially for games marked red.

`roms.ini` itself is an updated version with 1014 entries (1.1.8 had 970), more save types and entries for widescreen-patched ROMs.

`roms.ini` can also turn features off for games known to break with them: `PatchesEnabled=no`, `DynarecLoopOptimisation=no` and `MemoryAccessOptimisation=no` force those off for that game. Cruis'n USA uses this.

### Audio

- **Less crackling.** The audio buffer holds 4096 samples instead of 2048. The PSP plays 1024 at a time, so with the smaller buffer any slow frame ran it dry and clicked. The sample-rate converter now runs continuously instead of restarting every audio chunk, and pitch is always exact.
- **Soft gaps when a game runs slow.** If a game can't produce audio fast enough, playback fades out and waits until the buffer is half full before fading back in, instead of crackling.
- **Asynchronous audio no longer corrupts games.** The Media Engine (the PSP's second CPU) wrote its results to N64 memory through its own cache. That cache is written back in 64-byte blocks, which could overwrite game data next to the audio buffers (for example, Super Mario 64 crashing with sound on). It now writes to N64 memory directly.
- **Asynchronous audio never runs two audio lists at once.** If the Media Engine was still busy, the next list ran on the main CPU at the same time and both were corrupted. The main CPU now waits for the Media Engine, and switches to synchronous audio if it stops responding.

### Fixes

- **Dynarec:** `MTLO`/`MTHI` read the wrong register in compiled code, setting the multiply result registers to zero.
- **File Cache mode:** the last piece of a ROM whose size isn't a multiple of the cache chunk size was returned without being byte-swapped (wrong data for `.v64`/`.n64` ROMs).

### Bug reports

- **Crash screen in release builds.** 1.1.8 only enabled it in debug builds. Press X on the crash screen to save `exception.txt` with registers, PSP and N64 disassembly around the crash, and what the emulator was doing.
- **`hang.txt`** is written if a game stops showing new frames for about 4 seconds. It records the N64 code position, whether the N64 is still running, the graphics microcode and the last graphics command.
- **FPS + Timing.** Set **Display Info** to **FPS + Timing** in the global settings to show a breakdown under the FPS counter:
  - `CPU: INT DYN JIT OTH`: time spent interpreting, running compiled code, compiling, and other CPU work.
  - `GFX: DL VTX TEX DRAW`: display list parsing, vertex processing, textures and drawing.

  While it is on, the emulator also writes `perf.txt` (one line per second) and `dynarec.txt` (every ~10 seconds: the hottest interpreted and compiled code, failed compiles, fragment cache flushes and ROM loads) to the DaedalusX64 folder. With any other Display Info setting the timing code does nothing.

These files are the most useful thing to attach when reporting a slow or broken game.

## Usage

Copy the entire `DaedalusX64` folder to your Memory Stick, into `PSP/GAME`, and put your ROMs in `DaedalusX64/Roms`.

A PSP 2000 or later is recommended: it has enough memory for the larger ROM buffer and texture cache.

## Building

Install the PSP toolchain from https://github.com/pspdev/pspdev and make sure its `bin` folder is on your `PATH` and `PSPDEV` is set.

    ./build_daedalus.sh PSP_RELEASE    # Release build
    ./build_daedalus.sh PSP_DEBUG      # Debug build

The finished build is placed in the `DaedalusX64` folder.

Changes needed to build 1.1.8 with the current toolchain: `stricmp` is replaced with `strcasecmp`, a few type and struct field names changed in the SDK, and the link step no longer lists the C library by hand (that crashed at start-up).

## More info

This fork is based on the upstream DaedalusX64 project: https://github.com/DaedalusX64/daedalus

For compatibility lists and optimal settings, see the upstream wiki: https://github.com/DaedalusX64/daedalus/wiki

Upstream Discord: https://discord.gg/FrVTpBV

## Credits

- kreationz, salvy6735, Corn, Chilly Willy: Original DaedalusX64 code
- Wally: Optimizations, improvements and ports
- z2442: Compilation improvements and updating, optimizations
- mrneo240: Optimizations, compilation help
- TheMrIron2: Optimizations, wiki maintenance
