set(NESTUX_TEST_ROM "" CACHE FILEPATH "ROM used by the emulation smoke test, the test is skipped without one")

add_executable(emu_smoke_test smoke_test.cpp)
target_link_libraries(emu_smoke_test PRIVATE nestux_shell)

add_test(NAME emu_smoke
    COMMAND emu_smoke_test "${NESTUX_TEST_ROM}" "${NESTOPIA_ROOT}/NstDatabase.xml")
set_tests_properties(emu_smoke PROPERTIES
    SKIP_RETURN_CODE 77
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(debugger_smoke_test debugger_smoke_test.cpp)
target_link_libraries(debugger_smoke_test PRIVATE nestux_shell)

add_test(NAME debugger_smoke
    COMMAND debugger_smoke_test "${NESTUX_TEST_ROM}" "${NESTOPIA_ROOT}/NstDatabase.xml")
set_tests_properties(debugger_smoke PROPERTIES
    SKIP_RETURN_CODE 77
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(disassembler_test disassembler_test.cpp)
target_link_libraries(disassembler_test PRIVATE nestux_shell)
add_test(NAME disassembler COMMAND disassembler_test)

add_executable(debugger_condition_test debugger_condition_test.cpp)
target_link_libraries(debugger_condition_test PRIVATE nestux_shell)
add_test(NAME debugger_condition COMMAND debugger_condition_test)

add_executable(debugger_widget_test debugger_widget_test.cpp)
target_link_libraries(debugger_widget_test PRIVATE nestux_shell)
add_test(NAME debugger_widget COMMAND debugger_widget_test)
set_tests_properties(debugger_widget PROPERTIES
    TIMEOUT 20
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(profiler_test profiler_test.cpp)
target_link_libraries(profiler_test PRIVATE nestux_shell)
add_test(NAME profiler COMMAND profiler_test)
set_tests_properties(profiler PROPERTIES
    SKIP_RETURN_CODE 77
    TIMEOUT 60
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SDL_AUDIODRIVER=dummy")

add_executable(gui_smoke_test gui_smoke_test.cpp)
target_link_libraries(gui_smoke_test PRIVATE nestux_shell)

add_test(NAME gui_smoke COMMAND gui_smoke_test "${NESTUX_TEST_ROM}")
set_tests_properties(gui_smoke PROPERTIES
    SKIP_RETURN_CODE 77
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SDL_AUDIODRIVER=dummy")

# Headless test ROM runner, a tool and not a ctest case
add_executable(nst_test_runner nst_test_runner.cpp)
target_link_libraries(nst_test_runner PRIVATE nestux_shell)
target_compile_definitions(nst_test_runner PRIVATE NST_RUNNER_DATABASE="${NESTOPIA_ROOT}/NstDatabase.xml")
