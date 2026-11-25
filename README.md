# Nestux

Nestux is a NES emulator that combines the [Nestopia](https://github.com/0ldsk00l/nestopia)
emulation core with a cross-platform Qt 6 and SDL2 GUI. The GUI and the debugging and
profiling tools are structured after [fceux](https://github.com/TASEmulators/fceux).

The goal of the project is to bring fceux debug tooling to a higher accuracy emulation
core.

Nestux is a standalone project and is not intended to be merged back into Nestopia. The
Win32 GUI of the original code base was removed, the last revision that has it is tagged
`win32-legacy`.

* `source/core`: the Nestopia emulation core, based on
  [JG Nestopia](https://gitlab.com/jgemu/nestopia) and extended with debugging hooks,
  see `Core updates` below.
* `qt`: the Qt and SDL2 GUI, see [qt/README.md](qt/README.md).

## Build

    cmake -S qt -B build
    cmake --build build -j
    ./build/nestux [rom.nes]

Build options and tests are described in [qt/README.md](qt/README.md).

## Core updates

JG snapshots are kept unmodified on the `vendor/jg` branch (`source/core`,
`source/nes_ntsc`, `NstDatabase.xml`, `LICENSES`), with the JG commit hash in the commit
message. To take core fixes from JG Nestopia, commit the new snapshot on that branch and
merge it. The branches have unrelated histories, the first merge made the imported
snapshot the merge base.

Core changes made here stay on top of the vendored snapshot, so they show up as ordinary
merge results. Because the core is expected to diverge, such merges can need manual
conflict resolution.

## License

Nestux contains code from Nestopia, which is licensed under the GNU General Public
License, version 2 or later. Nestux is distributed under the same license. See `COPYING`,
`COPYRIGHT` and `LICENSES`.
