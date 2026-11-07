# Red Dead Revolver — PC static recompilation

An unofficial fan project that turns the PlayStation 2 game **Red Dead Revolver** (Rockstar San Diego, 2004) into a native Windows x64 executable. It uses static recompilation with [PS2Recomp](https://github.com/ran-j/PS2Recomp).

The MIPS R5900 code in the game's executable is translated **ahead of time** into C++. That C++ is compiled with MSVC and linked against `ps2xRuntime`, which reimplements the PS2 hardware and system: the EE kernel, IOP, GS, VU, DMA, SPU2 and CD/DVD.

> **No game files are included, and none ever will be.** You need your own disc image (USA, `SLUS-20500`) made from your own copy of the game. This repository contains only tooling, configuration, game-specific fixes and documentation.

## Status

| Milestone | Status |
|---|---|
| ELF identified, function map exported from Ghidra | ✅ |
| C++ generated and native executable built | ✅ |
| Boot: EE kernel, threads, IOP modules, disc reads | ✅ |
| Logos, main menu, controller input | ✅ |
| FMV playback | ✅ |
| Levels load and are playable | ✅ |
| Audio | ⏳ in progress |
| Full speed (≥ 30 FPS everywhere) | ⏳ some levels still drop below |

Known issues and the current investigation are tracked in [docs/ESTADO.md](docs/ESTADO.md). The docs are in Portuguese.

## PC version features (v1.0.0)

- **Launcher** (`RDR Launcher.exe`) styled after the game. You pick your own ISO once, and the game files are installed into folders by type (`movies\`, `audio\`, `levels\`, …). The layout is modeled on the classic PC releases.
- **Direct game executable** (`RedDeadRevolver.exe`). It starts the game at native resolution, fullscreen and widescreen.
- **Saves** go to `Documents\Red Dead Revolver`. There are no memory card prompts.
- **Start menu shortcuts** for the game, the launcher and the uninstaller.
- **Add to Steam** adds the game as a non-Steam shortcut with artwork, after installation.
- Gamepad (XInput) or keyboard and mouse.

## How it works

```
your ISO ──► ELF (SLUS_205.00, unpacked) ──► Ghidra function map
                                   │
                                   ▼
                     ps2_recomp + config/*.toml ──► generated C++  (ignored by git)
                                   │
                                   ▼
          ps2xRuntime + src/rdr_*.cpp overrides ──► CMake/Ninja/MSVC ──► native .exe
```

- **Fixes never touch the generated C++.** They go into the TOML config, instruction patches, game overrides in [src/](src/) (keyed by the ELF's CRC32), or the runtime.
- **Runtime changes** live in [patches/ps2recomp-runtime.patch](patches/ps2recomp-runtime.patch), on top of a pinned PS2Recomp commit (the `external/PS2Recomp` submodule).
- **Function addresses** are valid only for the exact build used: unpacked ELF CRC32 `EF570C27`.

## Repository layout

| Path | Contents |
|---|---|
| [config/](config/) | `ps2_recomp` configuration (stubs, skips, instruction patches) |
| [src/](src/) | game overrides, the launcher, the installer, Windows and Steam integration |
| [patches/](patches/) | changes to the PS2Recomp runtime |
| [scripts/](scripts/) | the pipeline: setup → recompile → build → run |
| [tools/](tools/) | measurement, profiling, packaging and Ghidra helpers |
| [docs/](docs/) | state of the project, architecture, identified functions (Portuguese) |
| `game/`, `ROM/`, `out/`, `logs/`, `dist/` | local only, ignored by git |

## Building

Requirements:

- Windows 10/11 x64
- Visual Studio 2022+ with the C++ workload
- CMake ≥ 3.20, Ninja, Git, Python 3
- A CPU with AVX
- 16 GB of RAM recommended (the generated C++ is tens of thousands of files)
- Optionally, Ghidra with a PS2 (R5900) loader, to regenerate the function map

```powershell
.\scripts\1_setup.ps1       # clones PS2Recomp at the pinned commit, applies the patch, builds the tools
.\scripts\2_recompile.ps1   # ELF + config -> out\generated (C++)
.\scripts\3_build.ps1       # builds the native runner (out\build)
.\scripts\4_run.ps1         # runs it and saves a log to logs\
.\tools\make_dist.ps1       # packages dist\RedDeadRevolver-v<version>.zip (no game data)
```

Put the executable from your own disc in `game/` before step 2. See [docs/](docs/) for the current state, the identified functions and the technical notes.

## Legal

This project is not affiliated with or endorsed by Rockstar Games, Take-Two Interactive or Sony. *Red Dead Revolver* is a trademark of its respective owners. No copyrighted game code, assets or data are distributed. Please don't open issues asking where to get the game.

## Credits

- [PS2Recomp](https://github.com/ran-j/PS2Recomp) by ran-j and contributors
- [PCSX2](https://github.com/PCSX2/pcsx2), the reference for hardware behavior
- Pipeline ideas from the [God of War](https://github.com/KIexster/god-of-war-recomp) and [ICO](https://github.com/nathanialf/ico-recomp) recomp projects
- Launcher font: Rye, SIL Open Font License
- Steam artwork: [SteamGridDB collection 34157](https://www.steamgriddb.com/collection/34157)
