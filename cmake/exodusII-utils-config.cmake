set(_pkg exodusII-utils)
set(_pkg_prefix exodusII_utils)

# Mirror CMake’s internal variables (replace '-' with '_')
# so we can safely use them in code.
set(${_pkg_prefix}_FIND_COMPONENTS ${${_pkg}_FIND_COMPONENTS})
foreach(_comp IN LISTS ${_pkg_prefix}_FIND_COMPONENTS)
    set(${_pkg_prefix}_FIND_REQUIRED_${_comp} ${${_pkg}_FIND_REQUIRED_${_comp}})
endforeach()

# Define known components
set(_${_pkg_prefix}_known_components exo-info exo-join)

foreach(_comp IN LISTS exodusII_utils_FIND_COMPONENTS)
    if (NOT _comp IN_LIST _exodusII_utils_known_components)
        message(FATAL_ERROR "Unknown component '${_comp}' requested from exodusII-utils")
    endif()

    # Find corresponding executable
    find_program(exodusII_utils_${_comp}_EXECUTABLE
        NAMES ${_comp}
        HINTS "${CMAKE_CURRENT_LIST_DIR}/../../bin"  # adjust if needed
    )

    if (NOT exodusII_utils_${_comp}_EXECUTABLE)
        if (exodusII_utils_FIND_REQUIRED_${_comp})
            message(FATAL_ERROR "Executable '${_comp}' not found (required by exodusII-utils)")
        endif()
    else()
        # Convenience alias
        set(exodusII_utils_${_comp} "${exodusII_utils_${_comp}_EXECUTABLE}")
        message(STATUS "Found exodusII-utils component: ${_comp} (${exodusII_utils_${_comp}})")
    endif()
endforeach()
