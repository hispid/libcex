# - Try to find the LibEvent config processing library
# Once done this will define
#
# LIBEVHTP_FOUND - System has LibEvent
# LIBEVHTP_INCLUDE_DIR - the LibEvent include directory
# LIBEVHTP_LIBRARIES - The libraries needed to use LibEvent

find_path     (LIBEVHTP_INCLUDE_DIR NAMES evhtp.h)
find_library  (LIBEVHTP_LIBRARY     NAMES libevhtp.a evhtp)

include (FindPackageHandleStandardArgs)

set (LIBEVHTP_INCLUDE_DIRS ${LIBEVHTP_INCLUDE_DIR})
set (LIBEVHTP_LIBRARIES ${LIBEVHTP_LIBRARY})

find_package_handle_standard_args (LibEvhtp DEFAULT_MSG LIBEVHTP_LIBRARIES LIBEVHTP_INCLUDE_DIR)
mark_as_advanced(LIBEVHTP_INCLUDE_DIRS LIBEVHTP_LIBRARIES)

# Check if WebSocket support is available (evhtp_ws.h exists)
if (LIBEVHTP_FOUND)
    find_path(EVHTP_WS_HEADER evhtp_ws.h
        PATHS ${LIBEVHTP_INCLUDE_DIR}
        PATH_SUFFIXES evhtp/ws
    )
    if(EVHTP_WS_HEADER)
        set(EVHTP_WS_SUPPORT ON CACHE INTERNAL "WebSocket support available in libevhtp")
        message(STATUS "WebSocket support found in libevhtp")
    endif()
    
    # Check if SSL support is available by reading config.h
    find_path(EVHTP_CONFIG_DIR config.h
        PATHS ${LIBEVHTP_INCLUDE_DIR}
        PATH_SUFFIXES evhtp
        NO_DEFAULT_PATH
    )
    if(EVHTP_CONFIG_DIR)
        # Read config.h and check if EVHTP_DISABLE_SSL is defined (not just #undef)
        file(READ "${EVHTP_CONFIG_DIR}/config.h" EVHTP_CONFIG_CONTENT)
        # Check for #define EVHTP_DISABLE_SSL (not preceded by #undef on the same logical line)
        # We look for #define that is not immediately after #undef
        string(REGEX MATCH "#define[ \t]+EVHTP_DISABLE_SSL" EVHTP_SSL_DISABLED "${EVHTP_CONFIG_CONTENT}")
        if(EVHTP_SSL_DISABLED)
            # EVHTP_DISABLE_SSL is defined - SSL is disabled
            set(LIBEVHTP_SSL_SUPPORT OFF CACHE INTERNAL "SSL support not available in libevhtp")
            message(STATUS "SSL support NOT found in libevhtp (EVHTP_DISABLE_SSL defined)")
        else()
            # EVHTP_DISABLE_SSL is not defined - SSL is enabled
            set(LIBEVHTP_SSL_SUPPORT ON CACHE INTERNAL "SSL support available in libevhtp")
            message(STATUS "SSL support found in libevhtp")
        endif()
    else()
        # config.h not found, assume SSL might be available but warn
        set(LIBEVHTP_SSL_SUPPORT OFF CACHE INTERNAL "SSL support unknown in libevhtp (config.h not found)")
        message(WARNING "libevhtp config.h not found - assuming SSL is disabled")
    endif()
endif ()
