cmake_minimum_required(VERSION 3.20)

project(Server C)

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

add_executable(Server
        server_logic.c
        http_lib.c
        process.c
        booking.c
        contact_validation.c
        booking_database.c
        auth.c
        form_urlencoded.c
        server_config.c
        rate_limit.c
        calendar_time.c
        calendar_database.c
        availability.c
        calendar_public.c
        admin_calendar.c
        admin_appointments.c
        notification_queue.c
        contact_links.c
)

add_executable(notification_worker
        notification_worker.c
        notification_queue.c
        booking_database.c
        calendar_time.c
        calendar_database.c
        contact_validation.c
        server_config.c
)

target_include_directories(notification_worker PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(create_admin_user
        tools/create_admin_user.c
)

add_executable(calendar_engine_tests EXCLUDE_FROM_ALL
        tests/calendar_engine_tests.c
        booking_database.c
        calendar_time.c
        calendar_database.c
        availability.c
        contact_validation.c
        server_config.c
)

target_include_directories(calendar_engine_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(calendar_fuzz_target EXCLUDE_FROM_ALL
        fuzzing/afl/calendar_fuzz_target.c
        booking_database.c
        calendar_time.c
        calendar_database.c
        availability.c
        contact_validation.c
        server_config.c
        form_urlencoded.c
        http_lib.c
)

target_include_directories(calendar_fuzz_target PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_include_directories(Server PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(Server PRIVATE
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

target_compile_definitions(notification_worker PRIVATE
        _POSIX_C_SOURCE=200809L
        _XOPEN_SOURCE=700
        SERVER_DEFAULT_BIND_ADDRESS="${STYLES4DOGS_BIND_ADDRESS}"
        SERVER_DEFAULT_PORT="${STYLES4DOGS_PORT}"
        SERVER_DEFAULT_DOCUMENT_ROOT="${STYLES4DOGS_DOCUMENT_ROOT}"
        SERVER_DEFAULT_SECRETS_DIR="${STYLES4DOGS_SECRETS_DIR}"
        SERVER_DEFAULT_AUTH_FILE="${STYLES4DOGS_AUTH_FILE}"
        SERVER_DEFAULT_DATA_DIR="${STYLES4DOGS_DATA_DIR}"
        SERVER_DEFAULT_DATABASE_FILE="${STYLES4DOGS_DATABASE_FILE}"
        SERVER_DEFAULT_LEGACY_BOOKING_FILE="${STYLES4DOGS_BOOKING_FILE}"
)

target_compile_definitions(calendar_engine_tests PRIVATE
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

target_compile_definitions(calendar_fuzz_target PRIVATE
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

target_link_libraries(Server PRIVATE
        PkgConfig::SODIUM
        PkgConfig::SQLITE3
)

target_link_libraries(notification_worker PRIVATE
        PkgConfig::SQLITE3
        PkgConfig::CURL
)

target_link_libraries(create_admin_user PRIVATE
        PkgConfig::SODIUM
)

target_link_libraries(calendar_engine_tests PRIVATE
        PkgConfig::SQLITE3
)

target_link_libraries(calendar_fuzz_target PRIVATE
        PkgConfig::SQLITE3
)

if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(Server PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Wstrict-prototypes
    )

    target_compile_options(notification_worker PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Wstrict-prototypes
    )

    target_compile_options(create_admin_user PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Wstrict-prototypes
    )

    target_compile_options(calendar_engine_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Wstrict-prototypes
    )

    target_compile_options(calendar_fuzz_target PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Wstrict-prototypes
    )
endif()
