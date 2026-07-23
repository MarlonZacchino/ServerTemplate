cmake_minimum_required(VERSION 3.20)

project(Server LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SODIUM REQUIRED IMPORTED_TARGET libsodium)
pkg_check_modules(SQLITE3 REQUIRED IMPORTED_TARGET sqlite3)
pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)

set(
        STYLES4DOGS_BIND_ADDRESS
        "127.0.0.1"
        CACHE STRING
        "Default IPv4 address used when STYLES4DOGS_BIND_ADDRESS is unset"
)
set(
        STYLES4DOGS_PORT
        "31337"
        CACHE STRING
        "Default TCP port used when STYLES4DOGS_PORT is unset"
)
set(
        STYLES4DOGS_DOCUMENT_ROOT
        "${CMAKE_CURRENT_SOURCE_DIR}/public"
        CACHE PATH
        "Directory containing public website files"
)
set(
        STYLES4DOGS_SECRETS_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/.secrets"
        CACHE PATH
        "Directory containing server secrets"
)
set(
        STYLES4DOGS_AUTH_FILE
        "${STYLES4DOGS_SECRETS_DIR}/admin.auth"
        CACHE FILEPATH
        "Admin authentication file"
)
set(
        STYLES4DOGS_DATA_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/data"
        CACHE PATH
        "Directory containing runtime data"
)
set(
        STYLES4DOGS_DATABASE_FILE
        "${STYLES4DOGS_DATA_DIR}/styles4dogs.db"
        CACHE FILEPATH
        "SQLite database file"
)
set(
        STYLES4DOGS_BOOKING_FILE
        "${STYLES4DOGS_DATA_DIR}/bookings.txt"
        CACHE FILEPATH
        "Legacy TSV booking file imported once into SQLite"
)

function(styles4dogs_enable_warnings target_name)
    if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(
                "${target_name}"
                PRIVATE
                -Wall
                -Wextra
                -Wpedantic
                -Wshadow
                -Wstrict-prototypes
        )
    endif()
endfunction()

set(STYLES4DOGS_CORE_SOURCES
        src/admin/admin_appointments.c
        src/admin/admin_calendar.c
        src/admin/admin_dashboard.c
        src/admin/admin_notifications.c
        src/booking/booking.c
        src/booking/booking_database.c
        src/booking/customer_portal.c
        src/calendar/availability.c
        src/calendar/calendar_database.c
        src/calendar/calendar_public.c
        src/calendar/calendar_time.c
        src/core/server_config.c
        src/gallery/gallery.c
        src/http/form_urlencoded.c
        src/http/http_lib.c
        src/http/process.c
        src/notifications/notification_queue.c
        src/notifications/notification_settings.c
        src/notifications/notification_templates.c
        src/security/auth.c
        src/security/rate_limit.c
        src/services/contact_links.c
        src/services/contact_validation.c
        src/services/postal_lookup.c
)

add_library(styles4dogs_core STATIC ${STYLES4DOGS_CORE_SOURCES})
add_library(styles4dogs::core ALIAS styles4dogs_core)

target_include_directories(
        styles4dogs_core
        PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_compile_definitions(
        styles4dogs_core
        PUBLIC
        _POSIX_C_SOURCE=200809L
        _XOPEN_SOURCE=700
        SERVER_DEFAULT_BIND_ADDRESS=\"${STYLES4DOGS_BIND_ADDRESS}\"
        SERVER_DEFAULT_PORT=\"${STYLES4DOGS_PORT}\"
        SERVER_DEFAULT_DOCUMENT_ROOT=\"${STYLES4DOGS_DOCUMENT_ROOT}\"
        SERVER_DEFAULT_SECRETS_DIR=\"${STYLES4DOGS_SECRETS_DIR}\"
        SERVER_DEFAULT_AUTH_FILE=\"${STYLES4DOGS_AUTH_FILE}\"
        SERVER_DEFAULT_DATA_DIR=\"${STYLES4DOGS_DATA_DIR}\"
        SERVER_DEFAULT_DATABASE_FILE=\"${STYLES4DOGS_DATABASE_FILE}\"
        SERVER_DEFAULT_LEGACY_BOOKING_FILE=\"${STYLES4DOGS_BOOKING_FILE}\"
)

target_link_libraries(
        styles4dogs_core
        PUBLIC
        PkgConfig::SODIUM
        PkgConfig::SQLITE3
        PkgConfig::CURL
)
styles4dogs_enable_warnings(styles4dogs_core)

add_executable(Server src/app/server_main.c)
target_link_libraries(Server PRIVATE styles4dogs::core)
styles4dogs_enable_warnings(Server)

add_executable(notification_worker src/app/notification_worker_main.c)
target_link_libraries(notification_worker PRIVATE styles4dogs::core)
styles4dogs_enable_warnings(notification_worker)

add_executable(create_admin_user tools/create_admin_user.c)
target_link_libraries(create_admin_user PRIVATE PkgConfig::SODIUM)
styles4dogs_enable_warnings(create_admin_user)

add_executable(
        calendar_engine_tests
        EXCLUDE_FROM_ALL
        tests/calendar_engine_tests.c
)
target_link_libraries(calendar_engine_tests PRIVATE styles4dogs::core)
styles4dogs_enable_warnings(calendar_engine_tests)

add_executable(
        calendar_fuzz_target
        EXCLUDE_FROM_ALL
        fuzzing/afl/calendar_fuzz_target.c
)
target_link_libraries(calendar_fuzz_target PRIVATE styles4dogs::core)
styles4dogs_enable_warnings(calendar_fuzz_target)

find_package(Doxygen QUIET)
if (Doxygen_FOUND)
    set(DOXYGEN_IN "${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile.in")
    set(DOXYGEN_OUT "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile")
    configure_file("${DOXYGEN_IN}" "${DOXYGEN_OUT}" @ONLY)
    add_custom_target(
            documentation
            COMMAND "${DOXYGEN_EXECUTABLE}" "${DOXYGEN_OUT}"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            COMMENT "Generating API documentation with Doxygen"
            VERBATIM
    )
endif()
