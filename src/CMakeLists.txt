set(VISOR_DEFINITIONS
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE _UNICODE
    _WIN32_WINNT=0x0A00
    VISOR_SHELL_VERSION="${PROJECT_VERSION}"
)
set(VISOR_COMPILE_OPTIONS /W4 /permissive- /utf-8 /Zc:__cplusplus)

# Release builds keep PDBs so crashes in the VM can be symbolised.
set(VISOR_RELEASE_PDB
    $<$<CONFIG:Release>:/Zi>
)
set(VISOR_RELEASE_PDB_LINK
    $<$<CONFIG:Release>:/DEBUG /OPT:REF /OPT:ICF>
)

# visor-session: the program Winlogon starts. Plain Win32 with the CRT linked
# statically, so it runs even if the Qt or VC++ runtime DLLs are missing, and
# can always fall back to Explorer.
add_executable(visor-session WIN32
    common/exitcodes.h
    session/main.cpp
)
set_target_properties(visor-session PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)
target_include_directories(visor-session PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(visor-session PRIVATE ${VISOR_DEFINITIONS})
target_compile_options(visor-session PRIVATE ${VISOR_COMPILE_OPTIONS} ${VISOR_RELEASE_PDB})
target_link_options(visor-session PRIVATE ${VISOR_RELEASE_PDB_LINK})
target_link_libraries(visor-session PRIVATE shell32 ole32)

# visor-shell: the shell services process (desktop window, hotkeys, and later
# tray, appbars, tasks). QtCore only; no UI toolkit.
qt_add_executable(visor-shell WIN32
    common/exitcodes.h
    common/linkprotocol.h
    shell/main.cpp
    shell/log.h shell/log.cpp
    shell/launch.h shell/launch.cpp
    shell/desktopwindow.h shell/desktopwindow.cpp
    shell/hotkeys.h shell/hotkeys.cpp
    shell/wallpaper.h shell/wallpaper.cpp
    shell/tasks.h shell/tasks.cpp
    shell/visorlink.h shell/visorlink.cpp
    shell/appbars.h shell/appbars.cpp
    shell/trayhost.h shell/trayhost.cpp
)
target_include_directories(visor-shell PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(visor-shell PRIVATE ${VISOR_DEFINITIONS})
target_compile_options(visor-shell PRIVATE ${VISOR_COMPILE_OPTIONS} ${VISOR_RELEASE_PDB})
target_link_options(visor-shell PRIVATE ${VISOR_RELEASE_PDB_LINK})
target_link_libraries(visor-shell PRIVATE
    Qt6::Core
    user32
    shell32
    ole32
    windowscodecs
    dwmapi
    shlwapi
)

# Copy the Qt runtime next to the exe so the build dir runs (and deploys)
# without Qt on PATH.
get_target_property(_qmake Qt6::qmake IMPORTED_LOCATION)
get_filename_component(_qt_bin "${_qmake}" DIRECTORY)
add_custom_command(TARGET visor-shell POST_BUILD
    COMMAND "${_qt_bin}/windeployqt.exe"
        --no-translations
        --no-system-d3d-compiler
        --no-system-dxc-compiler
        --no-opengl-sw
        --no-compiler-runtime
        --no-ffmpeg
        --verbose 0
        $<TARGET_FILE:visor-shell>
    VERBATIM
)
