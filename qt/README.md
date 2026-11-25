# Nestux

Nestux is a Qt 6 and SDL2 shell around the Nestopia core in `source/core`, structured
after the Qt GUI of [fceux](https://github.com/TASEmulators/fceux).

## Build

Requirements: CMake 3.21, a C++17 compiler, Qt 6 (Widgets, OpenGL, OpenGLWidgets,
Multimedia), SDL2, zlib. The Vulkan viewer is built when the Vulkan headers and Qt's
Vulkan support are found (`-DNESTUX_ENABLE_VULKAN=OFF` disables it).

From the repository root:

    cmake -S qt -B build
    cmake --build build -j
    ./build/nestux [rom.nes]

`NstDatabase.xml` is copied next to the executable at build time and installed with it.
Battery RAM is stored as `<rom name>.sav` in the `saves` folder of the application data
directory. The settings and data location is named `NestopiaQt`.

## Tests

    cmake -S qt -B build -DNESTUX_TEST_ROM=/path/to/any.nes
    cd build && ctest --output-on-failure

Without a ROM the ROM-dependent tests are skipped. `emu_smoke` runs the emulation
thread against a fake audio device and checks pacing, the frame signal, the emulation
mutex, pausing, audio rate matching and keyboard and (virtual) joystick input.
`debugger_smoke` runs a generated NROM program on the emulation thread with the debugger
enabled and checks halting, every run request, memory peeks, registers, the halt guard
and stopping while halted. With a ROM it also checks that breaking on many instructions
and inspecting the machine leaves the emulated frames and RAM unchanged.
`disassembler` checks the 6502 disassembler and needs no ROM. `gui_smoke` builds the main
window and input dialog on the offscreen platform, then checks debugger breakpoint and
bookmark edits, break/step controls, and opening and closing the debugger window.
`debugger_widget` uses a generated program to test assembly navigation, breakpoint
gutter interaction, register and flag edits, layout presets and saved tab placement.
It needs no external ROM.
Setting `NESTUX_TEST_AUDIO=sdl` or `qt` runs `emu_smoke` against a real audio device.

## Accuracy runner

`nst_test_runner` runs test ROMs headless, one child process per ROM and several in
parallel, and prints a result per ROM:

    ./build/tests/nst_test_runner --manifest /path/to/nes-test-roms/test_roms.xml
    ./build/tests/nst_test_runner /path/to/nes-test-roms/instr_test-v5
    ./build/tests/nst_test_runner --manifest ... --baseline qt/tests/baselines/nes-test-roms.tsv

* `pass` and `fail:XX`: the ROM reports through the blargg protocol, status byte at
  `$6000` and text from `$6004`, valid while `$6001-$6003` hold `DE B0 61`. Status `$81`
  makes the runner reset the machine 10 frames later, as the protocol asks. Because
  these ROMs usually declare no PRG RAM, the runner patches a copy of the image in
  memory to declare 8K, `--keep-header` turns that off.
* `screen:<hash>`: the ROM only shows its result on screen. The runner runs the number of
  frames of the manifest (`--screen-frames` without a manifest) and hashes the picture.
  The hash only tells whether the picture changed, not whether it is correct.
* `timeout`, `loaderror`, `crash`, `wall-timeout`, `missing`.
* `--baseline` compares every result with a saved file and exits with 1 on a difference,
  `--write-baseline` writes one, `--tsv` writes everything including messages and hashes.
  `qt/tests/baselines/nes-test-roms.tsv` holds the results for `test_roms.xml` of the
  nes-test-roms collection. Screen hashes depend on the core, so a changed core changes
  them on purpose and the file is regenerated after checking the pictures.
* The REF column is the result recorded in the manifest. A `!` marks a ROM where the
  protocol result differs from it.

## Threading model

* `EmulationThread` runs `NesCore` on its own thread and paces it with a monotonic
  clock to the frame rate of the loaded image (60.0988 Hz NTSC, 50.0070 Hz PAL).
* There is one emulation mutex, `EmulationThread::mutex()`. It protects everything that
  belongs to the emulation state: `NesCore`, its frame buffer, the input bindings and
  the joystick state. The thread holds it while it runs a frame and releases it while it
  sleeps. The GUI locks it with `EmulationThread::Locker` for any access, including key
  events. The mutex is not recursive.
* `EmulationThread::frameFinished(quint64)` is emitted at the end of every frame, after
  the mutex was released. The main window receives it queued, locks the mutex to copy the
  frame into the viewer and schedules a repaint.
* The audio FIFO has its own small lock because the audio device thread consumes it. It
  is not part of the emulation state.
* With `setDebuggerEnabled(true)` the core runs a separate CPU loop that checks
  breakpoints and run requests before each instruction. The normal loop is unchanged
  while the debugger is off. A breakpoint or step halts the emulation thread inside the
  frame: it emits `halted(address, reason)` and waits with the mutex released, so the
  GUI can lock the mutex and inspect the machine through `NesCore::debugger()`.
  `resume(request)` continues it and `requestBreak()` halts a running machine.
* Code that must change the machine while it may be halted (reset, loading or closing a
  ROM, changing the audio or video setup) first takes an `EmulationThread::HaltGuard`.
  It lets a halted frame finish and blocks new halts until it is destroyed.

## Debugger

Open **Debug > Debugger** to enable the debugger run loop. The dialog follows the
FCEUX Qt console debugger layout, with its own File, View, Debug and Options menus,
an icon-only toolbar, and tabbed CPU, PPU, Breakpoints and Bookmarks panels. The toolbar
and Debug menu share the same actions:

| Command | Shortcut |
|---------|----------|
| Run | F5 |
| Break | F6 |
| Step Into | F11 |
| Step Out | Shift+F11 |
| Step Over | F10 |
| Run to Cursor | Ctrl+F10 |
| Run Line (next scanline) | F7 |
| Run Frame | F8 |

**View > Layout Presets** provides Compact, Compact Split, Wide and Wide Quad.
Resize the splitters, reorder tabs, drag a tab into another visible group, or
right-click a tab and use **Move Tab To...** to choose any of eight panel positions.
Geometry, tab placement, splitter sizes, assembly font and display options are saved
when the dialog closes.

The assembly view scrolls through the CPU address space without snapping back on every
refresh. A new halt follows the PC, shown with an arrow and highlighted line. Click
the gutter to toggle an execute breakpoint. Double-click an address operand to follow
it without executing code; use Ctrl+Left / Ctrl+Right for navigation history and
Ctrl+G to return to the PC. Ctrl+A opens Go to Address. Arrow keys, Page Up/Down and
the wheel scroll instructions. Drag over lines and use Ctrl+C to copy them. The context
menu provides run-to-cursor, breakpoint, breakpoint properties, bookmark and copy
commands; B and M add/toggle a breakpoint or bookmark at the selected address. Options controls byte-code and mapped
PRG-offset display. Unofficial opcodes are marked with `*`.

CPU registers are hexadecimal fields and status flags are checkboxes, editable only
while halted. The PPU panel has read-only register and timing fields. Breakpoints can
be edited, disabled without deletion, or deleted. Bookmarks have optional names;
double-click one to navigate to it. Breakpoints currently match CPU addresses regardless
of the mapped PRG bank; a displayed PRG offset is inspection data, not a bank constraint.
Breakpoints and bookmarks remain session-local.

Add, Edit (or a double-click on a row) and the context menu command Breakpoint Properties
open a dialog with the CPU address, an optional condition and the enabled state. Add
prefills the selected assembly address, the context menu the address of the clicked line
and Edit the stored values. A breakpoint with a condition only halts when the expression
is non-zero before the instruction at the address runs, and is drawn in orange in the
gutter. The dialog rejects a syntax error and shows its column. Expressions use C
operators (`||  &&  |  ^  &  ==  !=  <  <=  >  >=  <<  >>  +  -  *  /  %`, unary `! ~ -`
and parentheses) on signed 64 bit integers, where division by zero gives 0. Numbers are
decimal, `$hex` or `0xhex`. `A X Y S P PC` read the registers, `[addr]` reads a byte and
`word[addr]` a little endian 16 bit value without side effects, and gives 0 where the
address cannot be read. Names are case insensitive, for example
`A == $10 && [$0010] != 0`. The same is available from the core API as the third
argument of `Debugger::SetExecBreakpoint`.

Assembly memory is captured when the dialog opens, the machine halts or changes, or
**View > Refresh Snapshot** is used. Scrolling and painting use this snapshot without
locking or reading the running emulator. Close the dialog to disable CPU debugger checks.

Memory inspection avoids I/O side effects. Addresses without a supported side-effect-free
read are shown as unknown. `.nl` labels and read/write breakpoints are not
implemented yet.

## Drivers

| Part  | Drivers | Notes |
|-------|---------|-------|
| Video | OpenGL (`QOpenGLWidget`), SDL, QWidget (`QPainter`), Vulkan (`QVulkanWindow`) | The SDL viewer embeds an SDL renderer in a native Qt window and needs the `xcb` Qt platform. It falls back to OpenGL on Wayland. Shaders of the Vulkan viewer are precompiled, see `src/video/shaders`. |
| Audio | SDL, Qt (`QAudioSink`) | Mono 16 bit. The FIFO fill level controls a linear resampler (at most 0.5 %) that matches the audio clock to the video timer. |
| Input | Keyboard, SDL joysticks | Two players, two joystick inputs and one key per NES button, configured in Options, Input. Joystick defaults are derived from the SDL game controller mapping when there is one. |

Video filters are the plain RGB renderer and the nes_ntsc filter (composite, S-Video and
RGB presets).

## Profiler

Structured after the profiler of fceux. Scopes are timed with markers:

    NESTUX_PROFILE_FUNC("Text that describes the scope");

A marker is written to a thread local buffer at the start and at the end of the scope.
While no capture runs, a scope costs one relaxed load and a branch. `Debug, Profiler...`
opens the control dialog. A capture drains the buffers of all threads on a collector
thread and streams them to a `.nstprof` file (format documented in
`src/profiler/ProfilerCapture.h`). The dialog also opens captures in a timeline with one
lane per thread and nested markers: zoom and pan with the wheel and by dragging, a click
sets a time reference, `Frame Marker` selects the marker that counts as a frame (the
emulator frame by default), and `Find Marker` steps through markers by name, function or
length. `scripts/profiler_dump.py` prints a capture as text.

Markers are placed in the emulation thread, the frame handler, the viewers and the audio
device callback, and `GuiBusyProfiler` marks the time the GUI thread is busy. Setting
`NESTUX_EVENT_TALLY` in the environment prints event counts per receiver class every
5 seconds. `-DNESTUX_PROFILER_ENABLE=OFF` removes the markers and the capture tooling,
captures can still be viewed.

## Layout

    qt/src/core     NesCore (Nestopia API wrapper), EmulationThread
    src/video    Viewer interface and the four viewers
    src/audio    AudioDriverBase (FIFO, rate control), SDL and Qt backends
    src/input    InputManager, JoystickSystem, InputConfigDialog
    src/ui       MainWindow
    qt/src/debugger DebuggerWindow, assembly view, tabbed panels and 6502 disassembler
    src/profiler Profiler markers, capture writer and reader, timeline viewer, dialog
    scripts      profiler_dump.py
    tests        smoke tests

## Not implemented yet

Save states, FDS and NSF images, archives, zapper and other expansion devices, netplay,
cheats, `.nl` symbol loading and read/write breakpoints. The debugger window
provides disassembly, register and PPU state, stack, execute and conditional breakpoints, bookmarks and
run control. Its memory view currently reports PRG offsets where the core can identify
the mapped ROM bank.
