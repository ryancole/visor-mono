# Settings shared by every executable. Sources include each other from src/,
# e.g. "common/linkprotocol.h" or "shell/tasks.h".
set(VISOR_DEFINITIONS
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE _UNICODE
    _WIN32_WINNT=0x0A00
    VISOR_VERSION="${PROJECT_VERSION}"
)
set(VISOR_COMPILE_OPTIONS /W4 /permissive- /utf-8 /Zc:__cplusplus)

# Release builds keep PDBs so crashes in the VM can be symbolised.
set(VISOR_RELEASE_PDB $<$<CONFIG:Release>:/Zi>)
set(VISOR_RELEASE_PDB_LINK $<$<CONFIG:Release>:/DEBUG /OPT:REF /OPT:ICF>)

get_target_property(_qmake Qt6::qmake IMPORTED_LOCATION)
get_filename_component(VISOR_QT_BIN "${_qmake}" DIRECTORY)

add_subdirectory(session) # visor-session.exe: what Winlogon starts
add_subdirectory(shell)   # visor-shell.exe: shell services
add_subdirectory(wm)      # visor-wm.exe: the tiling window manager
add_subdirectory(bar)     # visor.exe: the status bar (all UI)
