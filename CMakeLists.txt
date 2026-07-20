cmake_minimum_required(VERSION 3.20)

project(Server C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SODIUM REQUIRED IMPORTED_TARGET libsodium)

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
        STYLES4DOGS_BOOKING_FILE
        "${STYLES4DOGS_DATA_DIR}/bookings.txt"
        CACHE FILEPATH
        "Booking storage file"
)

add_executable(Server
        server_logic.c
        http_lib.c
        process.c
        booking.c
        auth.c
        form_urlencoded.c
)

add_executable(create_admin_user
        tools/create_admin_user.c
)

target_include_directories(Server PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(Server PRIVATE
        _POSIX_C_SOURCE=200809L
        _XOPEN_SOURCE=700
        SERVER_DOCUMENT_ROOT=\"${STYLES4DOGS_DOCUMENT_ROOT}\"
        SERVER_SECRETS_DIR=\"${STYLES4DOGS_SECRETS_DIR}\"
        SERVER_AUTH_FILE=\"${STYLES4DOGS_AUTH_FILE}\"
        SERVER_DATA_DIR=\"${STYLES4DOGS_DATA_DIR}\"
        SERVER_BOOKING_FILE=\"${STYLES4DOGS_BOOKING_FILE}\"
)

target_link_libraries(Server PRIVATE
        PkgConfig::SODIUM
)

target_link_libraries(create_admin_user PRIVATE
        PkgConfig::SODIUM
)

if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(Server PRIVATE
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
endif()
