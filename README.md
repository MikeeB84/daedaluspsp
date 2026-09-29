# DaedalusX64

DaedalusX64 is a Nintendo 64 emulator for the Sony PSP.

This fork targets the PSP only. Support for 3DS, Vita, Linux, macOS and Windows has been removed so the codebase can focus on PSP performance.

## Features:

- Fast emulation using the PSP's MIPS dynarec, VFPU and Media Engine
- High compatibility

## Usage

Installing Daedalus:
Download the latest release from https://github.com/DaedalusX64/daedalus/releases or, if you dare, use the latest builds in the GitHub Actions section.

Copy the entire DaedalusX64 folder to your Memory Stick and put it into PSP/Game.

## Building

Fetch the latest PSP toolchain from https://github.com/pspdev/pspdev and make sure its `bin` folder is on your `PATH` and `PSPDEV` is set.

    ./build_daedalus.sh          # Release build
    ./build_daedalus.sh DEBUG    # Debug build (asserts, debug console, logging)

The finished build is placed in the `DaedalusX64` folder.

## CI
DaedalusX64 has CI; you can get the latest nightlies from the Actions tab.
Warning: these builds are sporadic at times.

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
