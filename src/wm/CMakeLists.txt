# visor-wm: the tiling window manager. QtCore only; no UI toolkit. Started
# and supervised by visor-shell in replace mode.
qt_add_executable(visor-wm WIN32
    ../common/exitcodes.h
    ../common/log.h ../common/log.cpp
    main.cpp
    config.h config.cpp
    keyhook.h keyhook.cpp
    layout.h layout.cpp
    manager.h manager.cpp
    windows.h windows.cpp
    ../common/launch.h ../common/launch.cpp
)
target_include_directories(visor-wm PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(visor-wm PRIVATE
    ${VISOR_DEFINITIONS}
    # Debug builds fall back to the in-repo wm.conf so edits there live-reload.
    $<$<CONFIG:Debug>:VISOR_DEV_CONFIG_DIR="${PROJECT_SOURCE_DIR}/src/bar/config">
)
target_compile_options(visor-wm PRIVATE ${VISOR_COMPILE_OPTIONS} ${VISOR_RELEASE_PDB})
target_link_options(visor-wm PRIVATE ${VISOR_RELEASE_PDB_LINK})
target_link_libraries(visor-wm PRIVATE
    Qt6::Core
    user32
    advapi32
    dwmapi
    shell32
    ole32
)
# The Qt runtime it needs (Qt6Core) is deployed next to visor-shell, and
# wm.conf ships in the bar's config folder (the visor-config target).
add_dependencies(visor-wm visor-shell)
