cmake_minimum_required(VERSION 3.21)

project(NestopiaQt VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_AUTOMOC ON)

option(NESTOPIAQT_ENABLE_VULKAN "Build the Vulkan video viewer when Qt and the Vulkan headers are available" ON)
option(NESTOPIAQT_BUILD_TESTS "Build the emulation thread smoke test" ON)
option(NESTOPIAQT_PROFILER_ENABLE "Build the profiler markers and capture tooling (near-zero cost while not capturing)" ON)

set(NESTOPIA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." CACHE PATH "Root of the Nestopia source tree")
cmake_path(NORMAL_PATH NESTOPIA_ROOT)

set(NESTOPIA_CORE_DIR "${NESTOPIA_ROOT}/source/core")
if(NOT EXISTS "${NESTOPIA_CORE_DIR}/NstCpu.cpp")
    message(FATAL_ERROR "No Nestopia core found in ${NESTOPIA_CORE_DIR}")
endif()
message(STATUS "Nestopia core: ${NESTOPIA_CORE_DIR}")

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets Multimedia)
find_package(SDL2 REQUIRED)
find_package(ZLIB REQUIRED)

set(NESTOPIAQT_HAVE_VULKAN OFF)
if(NESTOPIAQT_ENABLE_VULKAN)
    find_package(Vulkan)
    if(Vulkan_FOUND)
        include(CheckIncludeFileCXX)
        set(CMAKE_REQUIRED_LIBRARIES Qt6::Gui Vulkan::Vulkan)
        check_include_file_cxx("QtGui/qvulkanwindow.h" NESTOPIAQT_QT_HAS_VULKAN)
        unset(CMAKE_REQUIRED_LIBRARIES)
        if(NESTOPIAQT_QT_HAS_VULKAN)
            set(NESTOPIAQT_HAVE_VULKAN ON)
        endif()
    endif()
    if(NOT NESTOPIAQT_HAVE_VULKAN)
        message(STATUS "Vulkan viewer disabled: Vulkan headers or Qt Vulkan support not found")
    endif()
endif()

# --------------------------------------------------------------------------
# Nestopia core.
# --------------------------------------------------------------------------
file(GLOB_RECURSE NESTOPIA_CORE_SOURCES CONFIGURE_DEPENDS "${NESTOPIA_CORE_DIR}/*.cpp")

add_library(nestopia_core STATIC ${NESTOPIA_CORE_SOURCES})
target_include_directories(nestopia_core SYSTEM PUBLIC "${NESTOPIA_CORE_DIR}" "${NESTOPIA_CORE_DIR}/api")
target_link_libraries(nestopia_core PUBLIC ZLIB::ZLIB)
set_target_properties(nestopia_core PROPERTIES AUTOMOC OFF)
# The core predates modern compilers, keep its warnings out of our build log.
target_compile_options(nestopia_core PRIVATE -w)

# --------------------------------------------------------------------------
# Qt shell
# --------------------------------------------------------------------------
set(NESTOPIAQT_SHELL_SOURCES
    src/core/NesCore.cpp
    src/core/NesCore.h
    src/core/EmulationThread.cpp
    src/core/EmulationThread.h
    src/Settings.cpp
    src/Settings.h
    src/audio/AudioDriver.cpp
    src/audio/AudioDriver.h
    src/audio/AudioDriverSDL.cpp
    src/audio/AudioDriverSDL.h
    src/audio/AudioDriverQt.cpp
    src/audio/AudioDriverQt.h
    src/input/InputManager.cpp
    src/input/InputManager.h
    src/input/JoystickSystem.cpp
    src/input/JoystickSystem.h
    src/input/InputConfigDialog.cpp
    src/input/InputConfigDialog.h
    src/video/ViewerInterface.cpp
    src/video/ViewerInterface.h
    src/video/ViewerGL.cpp
    src/video/ViewerGL.h
    src/video/ViewerSDL.cpp
    src/video/ViewerSDL.h
    src/video/ViewerQWidget.cpp
    src/video/ViewerQWidget.h
    src/debugger/Disassembler.cpp
    src/debugger/Disassembler.h
    src/debugger/DebuggerWindow.cpp
    src/debugger/DebuggerWindow.h
    src/debugger/DisassemblyView.cpp
    src/debugger/DisassemblyView.h
    src/debugger/DebuggerTabs.cpp
    src/debugger/DebuggerTabs.h
    src/profiler/GuiBusyProfiler.cpp
    src/profiler/GuiBusyProfiler.h
    src/profiler/Profiler.cpp
    src/profiler/Profiler.h
    src/profiler/ProfilerCapture.cpp
    src/profiler/ProfilerCapture.h
    src/profiler/ProfilerCaptureFile.cpp
    src/profiler/ProfilerCaptureFile.h
    src/profiler/ProfilerCaptureFormat.h
    src/profiler/ProfilerDialog.cpp
    src/profiler/ProfilerDialog.h
    src/profiler/ProfilerViewer.cpp
    src/profiler/ProfilerViewer.h
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
)

if(NESTOPIAQT_HAVE_VULKAN)
    list(APPEND NESTOPIAQT_SHELL_SOURCES
        src/video/ViewerVulkan.cpp
        src/video/ViewerVulkan.h
        src/video/VulkanShaders.h
    )
endif()

add_library(nestopiaqt_shell STATIC ${NESTOPIAQT_SHELL_SOURCES})
target_include_directories(nestopiaqt_shell PUBLIC src)
target_link_libraries(nestopiaqt_shell PUBLIC
    nestopia_core
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets Qt6::Multimedia
    SDL2::SDL2
)
if(NESTOPIAQT_HAVE_VULKAN)
    target_compile_definitions(nestopiaqt_shell PUBLIC NESTOPIAQT_HAVE_VULKAN=1)
    target_link_libraries(nestopiaqt_shell PUBLIC Vulkan::Vulkan)
endif()
if(NESTOPIAQT_PROFILER_ENABLE)
    message(STATUS "Profiler: enabled")
    target_compile_definitions(nestopiaqt_shell PUBLIC NESTOPIAQT_PROFILER_ENABLE=1)
else()
    message(STATUS "Profiler: disabled, captures can still be viewed")
endif()
target_compile_options(nestopiaqt_shell PRIVATE -Wall -Wextra)
target_compile_definitions(nestopiaqt_shell PRIVATE NESTOPIAQT_DATABASE_FALLBACK="${NESTOPIA_ROOT}/NstDatabase.xml")

add_executable(nestopiaqt src/main.cpp)
target_link_libraries(nestopiaqt PRIVATE nestopiaqt_shell)
target_compile_options(nestopiaqt PRIVATE -Wall -Wextra)

# The cartridge database is looked up next to the executable.
add_custom_command(TARGET nestopiaqt POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${NESTOPIA_ROOT}/NstDatabase.xml" "$<TARGET_FILE_DIR:nestopiaqt>/NstDatabase.xml"
)

install(TARGETS nestopiaqt RUNTIME DESTINATION bin)
install(FILES "${NESTOPIA_ROOT}/NstDatabase.xml" DESTINATION bin)

if(NESTOPIAQT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
