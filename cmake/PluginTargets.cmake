# Shared plugin target list for CLI, GUI, and tests.

set(GIS_PLUGIN_TARGETS
    plugin_projection
    plugin_cutting
    plugin_matching
    plugin_processing
    plugin_raster_math
    plugin_raster_inspect
    plugin_raster_manage
    plugin_raster_render
    plugin_georef
    plugin_terrain
    plugin_classification
    plugin_spindex
    plugin_vector
)

function(gis_copy_plugins_to_target_dir TARGET_NAME DEST_DIR)
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${DEST_DIR}"
    )
    foreach(GIS_PLUGIN_TARGET IN LISTS GIS_PLUGIN_TARGETS)
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${GIS_PLUGIN_TARGET}>
                "${DEST_DIR}/$<TARGET_FILE_NAME:${GIS_PLUGIN_TARGET}>"
        )
    endforeach()
endfunction()
