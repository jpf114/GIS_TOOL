if(EXISTS "${_SRC}" AND NOT EXISTS "${_DST}")
    get_filename_component(_DST_DIR "${_DST}" DIRECTORY)
    file(COPY "${_SRC}" DESTINATION "${_DST_DIR}")
endif()
