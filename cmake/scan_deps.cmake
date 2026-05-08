cmake_policy(SET CMP0207 NEW)

file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES
        "D:/Code/MyProject/GIS_TOOL/build/release/src/cli/Release/gis-cli.exe"
        "D:/Code/MyProject/GIS_TOOL/build/release/src/gui/Release/gis-gui.exe"
    LIBRARIES
        "D:/Code/MyProject/GIS_TOOL/build/release/src/plugins/classification/Release/plugin_classification.dll"
        "D:/Code/MyProject/GIS_TOOL/build/release/src/plugins/cutting/Release/plugin_cutting.dll"
        "D:/Code/MyProject/GIS_TOOL/build/release/src/plugins/georef/Release/plugin_georef.dll"
        "D:/Code/MyProject/GIS_TOOL/build/release/src/plugins/matching/Release/plugin_matching.dll"
        "D:/Code/MyProject/GIS_TOOL/build/release/src/plugins/processing/Release/plugin_processing.dll"
        "D:/Code/MyProject/GIS_TOOL/build/release/src/plugins/projection/Release/plugin_projection.dll"
        "D:/Code/MyProject/GIS_TOOL/build/release/src/plugins/raster_inspect/Release/plugin_raster_inspect.dll"
        "D:/Code/MyProject/GIS_TOOL/build/release/src/plugins/raster_manage/Release/plugin_raster_manage.dll"
        "D:/Code/MyProject/GIS_TOOL/build/release/src/plugins/raster_math/Release/plugin_raster_math.dll"
        "D:/Code/MyProject/GIS_TOOL/build/release/src/plugins/raster_render/Release/plugin_raster_render.dll"
        "D:/Code/MyProject/GIS_TOOL/build/release/src/plugins/spindex/Release/plugin_spindex.dll"
        "D:/Code/MyProject/GIS_TOOL/build/release/src/plugins/terrain/Release/plugin_terrain.dll"
        "D:/Code/MyProject/GIS_TOOL/build/release/src/plugins/vector/Release/plugin_vector.dll"
    RESOLVED_DEPENDENCIES_VAR resolved
    UNRESOLVED_DEPENDENCIES_VAR unresolved
    DIRECTORIES
        "D:/Code/GitHubCode/vcpkg/installed/x64-windows/bin"
    PRE_EXCLUDE_REGEXES
        "api-ms-"
        "ext-ms-"
        "[Hh][Vv][Ss][Ii]file[Tt]rust"
    POST_EXCLUDE_REGEXES
        ".*/[Ww]indows/.*"
        ".*/[Ss]ystem32/.*"
        ".*/[Ss]ys[Ww]OW64/.*"
)

set(dll_names "")
foreach(dep IN LISTS resolved)
    get_filename_component(name "${dep}" NAME)
    string(TOLOWER "${name}" name_lower)
    list(APPEND dll_names "${name_lower}")
endforeach()
list(SORT dll_names)
list(REMOVE_DUPLICATES dll_names)

list(FILTER dll_names EXCLUDE REGEX "^(kernel32|user32|gdi32|shell32|advapi32|ole32|oleaut32|ws2_32|crypt32|secur32|shlwapi|bcrypt|ncrypt|dbghelp|iphlpapi|msvcp140|vcruntime140|concrt140|setupapi|userenv|winmm|version|wldap32|winspool)")

message("=== Required vcpkg DLLs (${CMAKE_MATCH_COUNT}) ===")
foreach(dll IN LISTS dll_names)
    message("    ${dll}")
endforeach()

message("=== Unresolved ===")
foreach(dep IN LISTS unresolved)
    message("    ${dep}")
endforeach()
