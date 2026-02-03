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

find_package_handle_standard_args (LIBEVHTP DEFAULT_MSG LIBEVHTP_LIBRARIES LIBEVHTP_INCLUDE_DIR)
mark_as_advanced(LIBEVHTP_INCLUDE_DIRS LIBEVHTP_LIBRARIES)

# Check if WebSocket support is available (evhtp_ws.h exists)
if (LIBEVHTP_FOUND)
    find_path(EVHTP_WS_HEADER evhtp_ws.h
        PATHS ${LIBEVHTP_INCLUDE_DIR}
        PATH_SUFFIXES evhtp/ws
    )
    if(EVHTP_WS_HEADER)
        set(EVHTP_WS_SUPPORT ON)
        message(STATUS "WebSocket support found in libevhtp")
    endif()
endif ()
