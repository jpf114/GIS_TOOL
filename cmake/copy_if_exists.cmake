if(EXISTS "${_SRC}")
    get_filename_component(_DST_DIR "${_DST}" DIRECTORY)
    file(COPY "${_SRC}" DESTINATION "${_DST_DIR}")
endif()
