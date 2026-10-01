qt_add_executable(visor WIN32
    main.cpp
    app.h app.cpp
    settings.h settings.cpp
    shell.h shell.cpp
    tray.h tray.cpp
    services/shelllink.h services/shelllink.cpp
    services/windowicons.h services/windowicons.cpp
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
        services/systemclock.h services/systemclock.cpp
        services/activewindow.h services/activewindow.cpp
        services/audio.h services/audio.cpp
        services/media.h services/media.cpp
        services/tasks.h services/tasks.cpp
)

# services/ is listed too: the generated QML type registration includes
# headers by bare file name.
target_include_directories(visor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/services
)

target_compile_definitions(visor PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE _UNICODE
    _WIN32_WINNT=0x0A00
    VISOR_VERSION="${PROJECT_VERSION}"
    # Debug builds fall back to the in-repo config so edits there live-reload.
    $<$<CONFIG:Debug>:VISOR_DEV_CONFIG_DIR="${CMAKE_CURRENT_SOURCE_DIR}/config">
)

target_compile_options(visor PRIVATE /W4 /permissive- /utf-8 /bigobj /Zc:__cplusplus)

target_link_libraries(visor PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    shell32
    ole32
    windowsapp
)

# Ship the default config next to the exe.
add_custom_command(TARGET visor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/config $<TARGET_FILE_DIR:visor>/config
    VERBATIM
)

# Copy the Qt runtime (DLLs, QML modules, platform plugin) next to the exe so it
# runs without Qt on PATH.
get_target_property(_qmake Qt6::qmake IMPORTED_LOCATION)
get_filename_component(_qt_bin "${_qmake}" DIRECTORY)
add_custom_command(TARGET visor POST_BUILD
    COMMAND "${_qt_bin}/windeployqt.exe"
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
