# Nestopia

A fork of Nestopia with a cross-platform Qt 6 GUI. The Win32 GUI was removed, the
last revision that has it is tagged `win32-legacy`.

* `source/core`: the Nestopia emulation core. It is synchronized with
  [JG Nestopia](https://gitlab.com/jgemu/nestopia) and is being extended with debugging
  hooks, see `Core updates` below.
* `qt`: Nestux, the Qt and SDL2 GUI, see [qt/README.md](qt/README.md).

## Build

    cmake -S qt -B build
    cmake --build build -j
    ./build/nestux [rom.nes]

## Core updates

JG snapshots are kept unmodified on the `vendor/jg` branch (`source/core`,
`source/nes_ntsc`, `NstDatabase.xml`, `LICENSES`), with the JG commit hash in the commit
message. To update the core, commit the new snapshot on that branch and merge it. The
branches have unrelated histories, the first merge made the imported snapshot the merge
base.

Core changes made here stay on top of the vendored snapshot, so they show up as ordinary
merge results.

## License

Nestopia is licensed under the GNU General Public License, version 2 or later. See
`COPYING`, `COPYRIGHT` and `LICENSES`.
