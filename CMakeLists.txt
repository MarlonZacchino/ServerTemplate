cmake_minimum_required(VERSION 3.20)

project(Server C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_executable(Server
        server_logic.c
        http_lib.c
        process.c
)

target_include_directories(Server PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(Server PRIVATE
        _POSIX_C_SOURCE=200809L
        _XOPEN_SOURCE=700
)

if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(Server PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Wstrict-prototypes
    )
endif()