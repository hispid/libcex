# - Try to find the LibEvent config processing library
# Once done this will define
#
# LIBEVENT_FOUND - System has LibEvent
# LIBEVENT_INCLUDE_DIR - the LibEvent include directory
# LIBEVENT_LIBRARIES - The libraries needed to use LibEvent

find_path     (LIBEVENT_INCLUDE_DIR NAMES event.h)
find_library  (LIBEVENT_CORE        NAMES event_core)
find_library  (LIBEVENT_EXTRA       NAMES event_extra)
find_library  (LIBEVENT_THREAD      NAMES event_pthreads)

if (NOT CEX_DISABLE_SSL)
    find_library (LIBEVENT_SSL      NAMES event_openssl)
endif()

include (FindPackageHandleStandardArgs)


set (LIBEVENT_INCLUDE_DIRS ${LIBEVENT_INCLUDE_DIR})
set (LIBEVENT_LIBRARIES
        ${LIBEVENT_SSL}
        ${LIBEVENT_CORE}
        ${LIBEVENT_EXTRA}
        ${LIBEVENT_THREAD}
        ${LIBEVENT_EXTRA})

find_package_handle_standard_args (LIBEVENT DEFAULT_MSG LIBEVENT_LIBRARIES LIBEVENT_INCLUDE_DIR)
mark_as_advanced(LIBEVENT_INCLUDE_DIRS LIBEVENT_LIBRARIES)
