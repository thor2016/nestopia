set(NESTOPIAQT_TEST_ROM "" CACHE FILEPATH "ROM used by the emulation smoke test, the test is skipped without one")

add_executable(emu_smoke_test smoke_test.cpp)
target_link_libraries(emu_smoke_test PRIVATE nestopiaqt_shell)

add_test(NAME emu_smoke
    COMMAND emu_smoke_test "${NESTOPIAQT_TEST_ROM}" "${NESTOPIA_ROOT}/NstDatabase.xml")
set_tests_properties(emu_smoke PROPERTIES
    SKIP_RETURN_CODE 77
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(gui_smoke_test gui_smoke_test.cpp)
target_link_libraries(gui_smoke_test PRIVATE nestopiaqt_shell)

add_test(NAME gui_smoke COMMAND gui_smoke_test "${NESTOPIAQT_TEST_ROM}")
set_tests_properties(gui_smoke PROPERTIES
    SKIP_RETURN_CODE 77
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SDL_AUDIODRIVER=dummy")
