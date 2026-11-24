# NestopiaQt

A Qt 6 and SDL2 shell around the Nestopia core in `source/core`, structured after the
Qt GUI of [fceux](https://github.com/TASEmulators/fceux).

## Build

Requirements: CMake 3.21, a C++17 compiler, Qt 6 (Widgets, OpenGL, OpenGLWidgets,
Multimedia), SDL2, zlib. The Vulkan viewer is built when the Vulkan headers and Qt's
Vulkan support are found (`-DNESTOPIAQT_ENABLE_VULKAN=OFF` disables it).

From the repository root:

    cmake -S qt -B build
    cmake --build build -j
    ./build/nestopiaqt [rom.nes]

`NstDatabase.xml` is copied next to the executable at build time and installed with it.
Battery RAM is stored as `<rom name>.sav` in the `saves` folder of the application data
directory.

## Tests

    cmake -S qt -B build -DNESTOPIAQT_TEST_ROM=/path/to/any.nes
    cd build && ctest --output-on-failure

Without a ROM both tests are skipped. `emu_smoke` runs the emulation thread against a
fake audio device and checks pacing, the frame signal, the emulation mutex, pausing,
audio rate matching and keyboard and (virtual) joystick input. `gui_smoke` builds the
main window and the input dialog on the offscreen platform. Setting
`NESTOPIAQT_TEST_AUDIO=sdl` or `qt` runs `emu_smoke` against a real audio device.

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

## Drivers

| Part  | Drivers | Notes |
|-------|---------|-------|
| Video | OpenGL (`QOpenGLWidget`), SDL, QWidget (`QPainter`), Vulkan (`QVulkanWindow`) | The SDL viewer embeds an SDL renderer in a native Qt window and needs the `xcb` Qt platform. It falls back to OpenGL on Wayland. Shaders of the Vulkan viewer are precompiled, see `src/video/shaders`. |
| Audio | SDL, Qt (`QAudioSink`) | Mono 16 bit. The FIFO fill level controls a linear resampler (at most 0.5 %) that matches the audio clock to the video timer. |
| Input | Keyboard, SDL joysticks | Two players, two joystick inputs and one key per NES button, configured in Options, Input. Joystick defaults are derived from the SDL game controller mapping when there is one. |

Video filters are the plain RGB renderer and the nes_ntsc filter (composite, S-Video and
RGB presets).

## Layout

    qt/src/core     NesCore (Nestopia API wrapper), EmulationThread
    src/video    Viewer interface and the four viewers
    src/audio    AudioDriverBase (FIFO, rate control), SDL and Qt backends
    src/input    InputManager, JoystickSystem, InputConfigDialog
    src/ui       MainWindow
    tests        smoke tests

## Not implemented yet

Save states, FDS and NSF images, archives, zapper and other expansion devices, netplay,
cheats and all debugging tools.
