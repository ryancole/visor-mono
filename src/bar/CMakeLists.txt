# visor: the status bar, configured in QML. Runs on its own (e.g. under
# Explorer) or with visor-shell, which feeds it tasks and tray icons.
qt_add_executable(visor WIN32
    ../common/linkprotocol.h
    main.cpp
    app.h app.cpp
    settings.h settings.cpp
    shell.h shell.cpp
    tray.h tray.cpp
    services/shelllink.h services/shelllink.cpp
    services/windowicons.h services/windowicons.cpp
    services/appindex.h services/appindex.cpp
    ../common/launch.h ../common/launch.cpp
    ../common/log.h ../common/log.cpp
    ../common/theme.h ../common/theme.cpp
    resources/resource.h
    resources/visor.rc
)

# Everything QML-facing lives in the `Visor` module, compiled into the exe.
# User configs `import Visor` to get these types.
qt_add_qml_module(visor
    URI Visor
    VERSION 1.0
    NO_RESOURCE_TARGET_PATH
    SOURCES
        shellroot.h
        visorapi.h visorapi.cpp
        panelwindow.h panelwindow.cpp
        popupwindow.h popupwindow.cpp
        services/systemclock.h services/systemclock.cpp
        services/activewindow.h services/activewindow.cpp
        services/audio.h services/audio.cpp
        services/media.h services/media.cpp
        services/tasks.h services/tasks.cpp
        services/systemtray.h services/systemtray.cpp
        services/workspaces.h services/workspaces.cpp
        services/shellcontrol.h services/shellcontrol.cpp
        services/keybindings.h services/keybindings.cpp
        services/apps.h services/apps.cpp
        services/themes.h services/themes.cpp
)

# services/ is listed too: the generated QML type registration includes
# headers by bare file name. src/ is for the shared common/ headers.
target_include_directories(visor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/services
    ${PROJECT_SOURCE_DIR}/src
)

target_compile_definitions(visor PRIVATE
    ${VISOR_DEFINITIONS}
    # Debug builds fall back to the in-repo config so edits there live-reload.
    $<$<CONFIG:Debug>:VISOR_DEV_CONFIG_DIR="${CMAKE_CURRENT_SOURCE_DIR}/config">
)

target_compile_options(visor PRIVATE ${VISOR_COMPILE_OPTIONS} /bigobj)

target_link_libraries(visor PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    shell32
    ole32
    advapi32
    dwmapi
    powrprof
    shlwapi
    windowsapp
)

# Ship the default config (Visor's QML and visor-wm's wm.conf) next to the
# exes. Runs on every build, so config edits reach the build dir (and the VM)
# even when no code changed.
add_custom_target(visor-config ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/config ${CMAKE_BINARY_DIR}/config
    VERBATIM
)

# Copy the Qt runtime (DLLs, QML modules, platform plugin) next to the exe so it
# runs without Qt on PATH.
add_custom_command(TARGET visor POST_BUILD
    COMMAND "${VISOR_QT_BIN}/windeployqt.exe"
        --qmldir ${CMAKE_CURRENT_SOURCE_DIR}/config
        --no-translations
        --no-system-d3d-compiler
        --no-system-dxc-compiler
        --no-opengl-sw
        --no-compiler-runtime
        --no-ffmpeg
        --verbose 0
        $<TARGET_FILE:visor>
    VERBATIM
)
