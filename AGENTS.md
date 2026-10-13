# AGENTS.md

P.T. PC Port: a C++20 port of P.T. with a Vulkan renderer on SDL3 (MoltenVK on macOS). The game reads every asset at run
time from the player's own extracted `CUSA01127` folder, which lives outside this repository.

## Build, run, test

`make help` lists the macOS targets; `README.md` (Building from source) has Windows and Linux.

- `make run game=<extracted CUSA01127 folder>` builds and starts the game in a window. It blocks until the game quits:
  run it in the background and read the log.
- `make test` runs `pt_tests` and `pt_crash_report_test`. The other unit tests are the `pt_*_test` targets in
  `CMakeLists.txt`. `make extractor` builds the fake PKG helper (not part of the game's build; .NET 10 SDK).
- The first configure downloads the pinned dependencies and takes a few minutes.

## Verifying a change

The log is the evidence. The game writes one line per milestone (input, floor, menu, renderer, exit code).

- A windowed build-folder run keeps `pt.ini`, saves and `pt.log` in `build/macos/data/`. A headless run writes `pt.log`
  to the working folder. `--log <file>` names the log in both.
- `--headless --frames <n>` runs without a window. `tools/walkthrough.py --exe build/macos/pt --game <folder> [scenario]`
  plays the scripted routes in `tests/walkthrough/` headless and checks the log for each milestone in order. The `voice`
  scenario speaks the word through macOS `say` (Windows SAPI elsewhere).
- `build/macos/pt --help` lists every flag. `--settings <copy of pt.ini>` gives a run its own settings and a cold pipeline
  cache next to the copy.
- A fault or abort ends the log with `crash: <signal> ... backtrace:`, the frames and `exit: code <128+signal> (crash)`
  (`src/engine/core/crash_report.cpp`); `kill -SEGV <pid>` on a headless run shows it.

## Conventions

- **Comments say why**, in prose, and name the issue or doc that explains the choice. Match the surrounding density.
- **A new command-line flag touches three places in `src/main.cpp`:** the `Options` struct, the `kOptionSpecs` table
  (which is `--help`) and `ParseOptions`.
- **Platform differences live in `src/engine/platform/`.** `docs/macos.md` and `docs/linux.md` hold a table of them:
  update the row when the behaviour changes. `os.h` is for calls beyond SDL; a difference SDL already covers (a property,
  a hint) stays in one helper beside its caller (`pt::vk::DisplayHdrForWindow`), not spread over `#ifdef`s.
- **One CPU architecture per macOS build.** CMake refuses a universal build (`cmake/MacOS.cmake`).
- **Game data stays outside the repository.** `.gitignore` covers the archive formats; point `--game` at a folder
  elsewhere.

## Gotchas

- **Editor diagnostics before a configure are noise.** `compile_commands.json` appears in `build/macos/` once CMake has
  run; "`SDL3/SDL.h` file not found" means it has not.
- **`mouse_captured` in `RunGame` means "the mouse drives the look".** It gates look and mouse buttons and is false in
  menus. With `[input] mouse_lock` off (the macOS default) it is true in play with no SDL relative mode
  (`docs/macos.md`, mouse in play).
- **A PKG is not a game folder.** `--game` wants `chunk1.psarc` and `texture.qar` (`pathid_list_ps4.bin` beside them
  enables the enhanced textures). The extraction helper in `installer/Extractor` opens fake PKGs only and refuses store
  PKGs by design (`docs/installer.md`). It is not part of the macOS build: `make extractor` builds it there with the .NET 10
  SDK (`tools/make_extractor.py`, the LibOrbisPkg steps of `tools/ci/release.py`).
