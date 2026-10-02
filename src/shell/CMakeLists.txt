# visor-shell: the shell services process (desktop, tray, app bars, tasks,
# hotkeys, supervision of Visor and visor-wm). QtCore only; no UI toolkit.
qt_add_executable(visor-shell WIN32
    ../common/exitcodes.h
    ../common/linkprotocol.h
    main.cpp
    ../common/log.h ../common/log.cpp
    ../common/launch.h ../common/launch.cpp
    supervisor.h supervisor.cpp
    desktopwindow.h desktopwindow.cpp
    hotkeys.h hotkeys.cpp
    wallpaper.h wallpaper.cpp
    tasks.h tasks.cpp
    visorlink.h visorlink.cpp
    appbars.h appbars.cpp
    trayhost.h trayhost.cpp
)
target_include_directories(visor-shell PRIVATE ${PROJECT_SOURCE_DIR}/src)
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

# Copy the Qt runtime next to the exe so the build dir runs without Qt on PATH.
add_custom_command(TARGET visor-shell POST_BUILD
    COMMAND "${VISOR_QT_BIN}/windeployqt.exe"
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
