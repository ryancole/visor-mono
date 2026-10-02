# visor-session: the program Winlogon starts. Plain Win32 with the CRT linked
# statically, so it runs even if the Qt or VC++ runtime DLLs are missing, and
# can always fall back to Explorer.
add_executable(visor-session WIN32
    ../common/exitcodes.h
    main.cpp
)
set_target_properties(visor-session PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)
target_include_directories(visor-session PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(visor-session PRIVATE ${VISOR_DEFINITIONS})
target_compile_options(visor-session PRIVATE ${VISOR_COMPILE_OPTIONS} ${VISOR_RELEASE_PDB})
target_link_options(visor-session PRIVATE ${VISOR_RELEASE_PDB_LINK})
target_link_libraries(visor-session PRIVATE shell32 ole32)
